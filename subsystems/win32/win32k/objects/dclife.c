/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           Functions for creation and destruction of DCs
 * FILE:              subsystem/win32/win32k/objects/dclife.c
 * PROGRAMER:         Timo Kreuzer (timo.kreuzer@rectos.org)
 */

#include <w32k.h>
#include <bugcodes.h>

#define NDEBUG
#include <debug.h>

//FIXME: windows uses 0x0012009f
#define DIRTY_DEFAULT DIRTY_CHARSET|DIRTY_BACKGROUND|DIRTY_TEXT|DIRTY_LINE|DIRTY_FILL

PSURFACE psurfDefaultBitmap = NULL;
PBRUSH pbrDefaultBrush = NULL;

// FIXME: these should go to floatobj.h or something
#define FLOATOBJ_0 {0x00000000, 0x00000000}
#define FLOATOBJ_1 {0x40000000, 0x00000002}
#define FLOATOBJ_16 {0x40000000, 0x00000006}
#define FLOATOBJ_1_16 {0x40000000, 0xfffffffe}

static const FLOATOBJ gef0 = FLOATOBJ_0;
static const FLOATOBJ gef1 = FLOATOBJ_1;
static const FLOATOBJ gef16 = FLOATOBJ_16;

static const MATRIX	gmxWorldToDeviceDefault =
{
    FLOATOBJ_16, FLOATOBJ_0,
    FLOATOBJ_0, FLOATOBJ_16,
    FLOATOBJ_0, FLOATOBJ_0,
    0, 0, 0x4b
};

static const MATRIX	gmxDeviceToWorldDefault =
{
    FLOATOBJ_1_16, FLOATOBJ_0,
    FLOATOBJ_0, FLOATOBJ_1_16,
    FLOATOBJ_0, FLOATOBJ_0,
    0, 0, 0x53
};

static const MATRIX	gmxWorldToPageDefault =
{
    FLOATOBJ_1, FLOATOBJ_0,
    FLOATOBJ_0, FLOATOBJ_1,
    FLOATOBJ_0, FLOATOBJ_0,
    0, 0, 0x63
};

// HACK!! Fix XFORMOBJ then use 1:16 / 16:1
#define gmxWorldToDeviceDefault gmxWorldToPageDefault
#define gmxDeviceToWorldDefault gmxWorldToPageDefault

/** Internal functions ********************************************************/

NTSTATUS
InitDcImpl()
{
    psurfDefaultBitmap = SURFACE_ShareLockSurface(StockObjects[DEFAULT_BITMAP]);
    if (!psurfDefaultBitmap)
        return STATUS_UNSUCCESSFUL;

    pbrDefaultBrush = BRUSH_ShareLockBrush(StockObjects[BLACK_BRUSH]);
    if (!pbrDefaultBrush)
        return STATUS_UNSUCCESSFUL;

    return STATUS_SUCCESS;
}


PDC
NTAPI
DC_AllocDcWithHandle()
{
    PDC pdc;
    pdc = (PDC)GDIOBJ_AllocObjWithHandle(GDILoObjType_LO_DC_TYPE);

    pdc->pdcattr = &pdc->dcattr;

    return pdc;
}


void
DC_InitHack(PDC pdc)
{
    HRGN hVisRgn;

    TextIntRealizeFont(pdc->pdcattr->hlfntNew,NULL);
    pdc->pdcattr->iCS_CP = ftGdiGetTextCharsetInfo(pdc,NULL,0);

    /* This should never fail */
    ASSERT(pdc->dclevel.ppal);

    /* Select regions */
    // FIXME: too complicated, broken error handling
    pdc->rosdc.hClipRgn = NULL;
    pdc->rosdc.hGCClipRgn = NULL;
    hVisRgn = NtGdiCreateRectRgn(0, 0, pdc->dclevel.sizl.cx, pdc->dclevel.sizl.cy);
    ASSERT(hVisRgn);
    GdiSelectVisRgn(pdc->BaseObject.hHmgr, hVisRgn);
    GreDeleteObject(hVisRgn);
    ASSERT(pdc->rosdc.hVisRgn);
    pdc->rosdc.bitsPerPixel = pdc->ppdev->gdiinfo.cBitsPixel *
                              pdc->ppdev->gdiinfo.cPlanes;
}

VOID
NTAPI
DC_vInitDc(
    PDC pdc,
    DCTYPE dctype,
    PPDEVOBJ ppdev)
{

    /* Setup some basic fields */
    pdc->dctype = dctype;
    pdc->ppdev = ppdev;
    pdc->dhpdev = ppdev->dhpdev;
    pdc->hsem = ppdev->hsemDevLock;
    pdc->flGraphicsCaps = ppdev->devinfo.flGraphicsCaps;
    pdc->flGraphicsCaps2 = ppdev->devinfo.flGraphicsCaps2;
    pdc->fs = DC_DIRTY_RAO;

    /* Setup dc attribute */
    pdc->pdcattr = &pdc->dcattr;
    pdc->dcattr.pvLDC = NULL;
    pdc->dcattr.ulDirty_ = DIRTY_DEFAULT;
    if (ppdev == gppdevPrimary)
        pdc->dcattr.ulDirty_ |= DC_PRIMARY_DISPLAY;

    /* Setup the DC size */
    if (dctype == DCTYPE_MEMORY)
    {
        /* Memory DCs have a 1 x 1 bitmap by default */
        pdc->dclevel.sizl.cx = 1;
        pdc->dclevel.sizl.cy = 1;
    }
    else
    {
        /* Other DC's are as big as the related PDEV */
	    pdc->dclevel.sizl.cx = ppdev->gdiinfo.ulHorzRes;
	    pdc->dclevel.sizl.cy = ppdev->gdiinfo.ulVertRes;
    }

    /* Setup Window rect based on DC size */
    pdc->erclWindow.left = 0;
    pdc->erclWindow.top = 0;
    pdc->erclWindow.right = pdc->dclevel.sizl.cx;
    pdc->erclWindow.bottom = pdc->dclevel.sizl.cy;

    if (dctype == DCTYPE_DIRECT)
    {
        /* Direct DCs get the surface from the PDEV */
        pdc->dclevel.pSurface = PDEVOBJ_pSurface(ppdev);

        pdc->erclBounds.left = 0x7fffffff;
        pdc->erclBounds.top = 0x7fffffff;
        pdc->erclBounds.right = 0x80000000;
        pdc->erclBounds.bottom = 0x80000000;
        pdc->erclBoundsApp.left = 0xffffffff;
        pdc->erclBoundsApp.top = 0xfffffffc;
        pdc->erclBoundsApp.right = 0x00007ffc; // FIXME
        pdc->erclBoundsApp.bottom = 0x00000333; // FIXME
        pdc->erclClip = pdc->erclBounds;
//        pdc->co

        pdc->fs |= DC_SYNCHRONIZEACCESS | DC_ACCUM_APP | DC_PERMANANT | DC_DISPLAY;
    }
    else
    {
        /* Non-direct DCs don't have a surface by default */
        pdc->dclevel.pSurface = NULL;

        // FIXME: HACK, because our code expects a surface
        pdc->dclevel.pSurface = SURFACE_ShareLockSurface(StockObjects[DEFAULT_BITMAP]);

        pdc->erclBounds.left = 0;
        pdc->erclBounds.top = 0;
        pdc->erclBounds.right = 0;
        pdc->erclBounds.bottom = 0;
        pdc->erclBoundsApp = pdc->erclBounds;
        pdc->erclClip = pdc->erclWindow;
//        pdc->co = NULL
    }

//        pdc->dcattr.VisRectRegion:

    /* Setup coordinate transformation data */
	pdc->dclevel.mxWorldToDevice = gmxWorldToDeviceDefault;
	pdc->dclevel.mxDeviceToWorld = gmxDeviceToWorldDefault;
	pdc->dclevel.mxWorldToPage = gmxWorldToPageDefault;
	pdc->dclevel.efM11PtoD = gef16;
	pdc->dclevel.efM22PtoD = gef16;
	pdc->dclevel.efDxPtoD = gef0;
	pdc->dclevel.efDyPtoD = gef0;
	pdc->dclevel.efM11_TWIPS = gef0;
	pdc->dclevel.efM22_TWIPS = gef0;
	pdc->dclevel.efPr11 = gef0;
	pdc->dclevel.efPr22 = gef0;
	pdc->dcattr.mxWorldToDevice = pdc->dclevel.mxWorldToDevice;
	pdc->dcattr.mxDeviceToWorld = pdc->dclevel.mxDeviceToWorld;
	pdc->dcattr.mxWorldToPage = pdc->dclevel.mxWorldToPage;
	pdc->dcattr.efM11PtoD = pdc->dclevel.efM11PtoD;
	pdc->dcattr.efM22PtoD = pdc->dclevel.efM22PtoD;
	pdc->dcattr.efDxPtoD = pdc->dclevel.efDxPtoD;
	pdc->dcattr.efDyPtoD = pdc->dclevel.efDyPtoD;
	pdc->dcattr.iMapMode = MM_TEXT;
	pdc->dcattr.dwLayout = 0;
	pdc->dcattr.flXform = PAGE_TO_DEVICE_SCALE_IDENTITY |
	                      PAGE_TO_DEVICE_IDENTITY |
	                      WORLD_TO_PAGE_IDENTITY;

    /* Setup more coordinates */
    pdc->ptlDCOrig.x = 0;
    pdc->ptlDCOrig.y = 0;
	pdc->dcattr.lWindowOrgx = 0;
	pdc->dcattr.ptlWindowOrg.x = 0;
	pdc->dcattr.ptlWindowOrg.y = 0;
	pdc->dcattr.szlWindowExt.cx = 1;
	pdc->dcattr.szlWindowExt.cy = 1;
	pdc->dcattr.ptlViewportOrg.x = 0;
	pdc->dcattr.ptlViewportOrg.y = 0;
	pdc->dcattr.szlViewportExt.cx = 1;
	pdc->dcattr.szlViewportExt.cy = 1;
    pdc->dcattr.szlVirtualDevicePixel.cx = 0;
    pdc->dcattr.szlVirtualDevicePixel.cy = 0;
    pdc->dcattr.szlVirtualDeviceMm.cx = 0;
    pdc->dcattr.szlVirtualDeviceMm.cy = 0;
    pdc->dcattr.szlVirtualDeviceSize.cx = 0;
    pdc->dcattr.szlVirtualDeviceSize.cy = 0;

    /* Setup regions */
    pdc->prgnAPI = NULL;
    pdc->prgnVis = NULL; // FIXME
    pdc->prgnRao = NULL;

    /* Setup palette */
    pdc->dclevel.hpal = StockObjects[DEFAULT_PALETTE];
    pdc->dclevel.ppal = PALETTE_ShareLockPalette(pdc->dclevel.hpal);

    /* Setup path */
	pdc->dclevel.hPath = NULL;
    pdc->dclevel.flPath = 0;
//	pdc->dclevel.lapath:

    /* Setup colors */
	pdc->dcattr.crBackgroundClr = RGB(0xff, 0xff, 0xff);
	pdc->dcattr.ulBackgroundClr = RGB(0xff, 0xff, 0xff);
	pdc->dcattr.crForegroundClr = RGB(0, 0, 0);
	pdc->dcattr.ulForegroundClr = RGB(0, 0, 0);
	pdc->dcattr.crBrushClr = RGB(0xff, 0xff, 0xff);
	pdc->dcattr.ulBrushClr = RGB(0xff, 0xff, 0xff);
	pdc->dcattr.crPenClr = RGB(0, 0, 0);
	pdc->dcattr.ulPenClr = RGB(0, 0, 0);

    /* Select the default fill and line brush */
	pdc->dcattr.hbrush = StockObjects[WHITE_BRUSH];
	pdc->dcattr.hpen = StockObjects[BLACK_PEN];
    pdc->dclevel.pbrFill = BRUSH_ShareLockBrush(pdc->pdcattr->hbrush);
    pdc->dclevel.pbrLine = PEN_ShareLockPen(pdc->pdcattr->hpen);
	pdc->dclevel.ptlBrushOrigin.x = 0;
	pdc->dclevel.ptlBrushOrigin.y = 0;
	pdc->dcattr.ptlBrushOrigin = pdc->dclevel.ptlBrushOrigin;

    /* Initialize EBRUSHOBJs */
    EBRUSHOBJ_vInit(&pdc->eboFill, pdc->dclevel.pbrFill, pdc);
    EBRUSHOBJ_vInit(&pdc->eboLine, pdc->dclevel.pbrLine, pdc);
    EBRUSHOBJ_vInit(&pdc->eboText, pbrDefaultBrush, pdc);
    EBRUSHOBJ_vInit(&pdc->eboBackground, pbrDefaultBrush, pdc);

    /* Setup fill data */
	pdc->dcattr.jROP2 = R2_COPYPEN;
	pdc->dcattr.jBkMode = 2;
	pdc->dcattr.lBkMode = 2;
	pdc->dcattr.jFillMode = ALTERNATE;
	pdc->dcattr.lFillMode = 1;
	pdc->dcattr.jStretchBltMode = 1;
	pdc->dcattr.lStretchBltMode = 1;
    pdc->ptlFillOrigin.x = 0;
    pdc->ptlFillOrigin.y = 0;

    /* Setup drawing position */
	pdc->dcattr.ptlCurrent.x = 0;
	pdc->dcattr.ptlCurrent.y = 0;
	pdc->dcattr.ptfxCurrent.x = 0;
	pdc->dcattr.ptfxCurrent.y = 0;

	/* Setup ICM data */
	pdc->dclevel.lIcmMode = 0;
	pdc->dcattr.lIcmMode = 0;
	pdc->dcattr.hcmXform = NULL;
	pdc->dcattr.flIcmFlags = 0;
	pdc->dcattr.IcmBrushColor = CLR_INVALID;
	pdc->dcattr.IcmPenColor = CLR_INVALID;
	pdc->dcattr.pvLIcm = NULL;
    pdc->dcattr.hColorSpace = NULL; // FIXME: 0189001f
	pdc->dclevel.pColorSpace = NULL; // FIXME
    pdc->pClrxFormLnk = NULL;
//	pdc->dclevel.ca =

	/* Setup font data */
    pdc->hlfntCur = NULL; // FIXME: 2f0a0cf8
    pdc->pPFFList = NULL;
    pdc->flSimulationFlags = 0;
    pdc->lEscapement = 0;
    pdc->prfnt = NULL;
	pdc->dcattr.flFontMapper = 0;
	pdc->dcattr.flTextAlign = 0;
	pdc->dcattr.lTextAlign = 0;
	pdc->dcattr.lTextExtra = 0;
	pdc->dcattr.lRelAbs = 1;
	pdc->dcattr.lBreakExtra = 0;
	pdc->dcattr.cBreak = 0;
    pdc->dcattr.hlfntNew = StockObjects[SYSTEM_FONT];
//	pdc->dclevel.pFont = LFONT_ShareLockFont(pdc->dcattr.hlfntNew);

    /* Other stuff */
    pdc->hdcNext = NULL;
    pdc->hdcPrev = NULL;
    pdc->ipfdDevMax = 0x0000ffff;
    pdc->ulCopyCount = -1;
    pdc->ptlDoBanding.x = 0;
    pdc->ptlDoBanding.y = 0;
	pdc->dclevel.lSaveDepth = 1;
	pdc->dclevel.hdcSave = NULL;
	pdc->dcattr.iGraphicsMode = GM_COMPATIBLE;
	pdc->dcattr.iCS_CP = 0;
    pdc->pSurfInfo = NULL;

}

BOOL
INTERNAL_CALL
DC_Cleanup(PVOID ObjectBody)
{
    PDC pdc = (PDC)ObjectBody;

    /* Free DC_ATTR */
    DC_vFreeDcAttr(pdc);

    /* Delete saved DCs */
    DC_vRestoreDC(pdc, 1);

    /* Deselect dc objects */
    DC_vSelectSurface(pdc, NULL);
    DC_vSelectFillBrush(pdc, NULL);
    DC_vSelectLineBrush(pdc, NULL);
    DC_vSelectPalette(pdc, NULL);

    /* Cleanup the dc brushes */
    EBRUSHOBJ_vCleanup(&pdc->eboFill);
    EBRUSHOBJ_vCleanup(&pdc->eboLine);
    EBRUSHOBJ_vCleanup(&pdc->eboText);
    EBRUSHOBJ_vCleanup(&pdc->eboBackground);

    /*  Free regions */
    if (pdc->rosdc.hClipRgn)
        GreDeleteObject(pdc->rosdc.hClipRgn);
    if (pdc->rosdc.hVisRgn)
        GreDeleteObject(pdc->rosdc.hVisRgn);
ASSERT(pdc->rosdc.hGCClipRgn);
    if (pdc->rosdc.hGCClipRgn)
        GreDeleteObject(pdc->rosdc.hGCClipRgn);
    if (NULL != pdc->rosdc.CombinedClip)
        IntEngDeleteClipRegion(pdc->rosdc.CombinedClip);

    PATH_Delete(pdc->dclevel.hPath);

    return TRUE;
}

BOOL
FASTCALL
DC_SetOwnership(HDC hDC, PEPROCESS Owner)
{
    INT Index;
    PGDI_TABLE_ENTRY Entry;
    PDC pDC;

    /* FIXME: This function has broken error handling */

    if (!GDIOBJ_SetOwnership(hDC, Owner))
    {
        DPRINT1("GDIOBJ_SetOwnership failed\n");
        return FALSE;
    }

    pDC = DC_LockDc(hDC);
    if (!pDC)
    {
        DPRINT1("Could not lock DC\n");
        return FALSE;
    }

    /*
       System Regions:
          These regions do not use attribute sections and when allocated, use
          gdiobj level functions.
    */
        if (pDC->rosdc.hClipRgn)
        {   // FIXME! HAX!!!
            Index = GDI_HANDLE_GET_INDEX(pDC->rosdc.hClipRgn);
            Entry = &GdiHandleTable->Entries[Index];
            if (Entry->UserData) FreeObjectAttr(Entry->UserData);
            Entry->UserData = NULL;
            //
            if (!GDIOBJ_SetOwnership(pDC->rosdc.hClipRgn, Owner)) return FALSE;
        }
        if (pDC->rosdc.hVisRgn)
        {   // FIXME! HAX!!!
            Index = GDI_HANDLE_GET_INDEX(pDC->rosdc.hVisRgn);
            Entry = &GdiHandleTable->Entries[Index];
            if (Entry->UserData) FreeObjectAttr(Entry->UserData);
            Entry->UserData = NULL;
            //
            if (!GDIOBJ_SetOwnership(pDC->rosdc.hVisRgn, Owner)) return FALSE;
        }
        if (pDC->rosdc.hGCClipRgn)
        {   // FIXME! HAX!!!
            Index = GDI_HANDLE_GET_INDEX(pDC->rosdc.hGCClipRgn);
            Entry = &GdiHandleTable->Entries[Index];
            if (Entry->UserData) FreeObjectAttr(Entry->UserData);
            Entry->UserData = NULL;
            //
            if (!GDIOBJ_SetOwnership(pDC->rosdc.hGCClipRgn, Owner)) return FALSE;
        }
        if (pDC->dclevel.hPath)
        {
            if (!GDIOBJ_SetOwnership(pDC->dclevel.hPath, Owner)) return FALSE;
        }
        DC_UnlockDc(pDC);

    return TRUE;
}

HDC
NTAPI
GreOpenDCW(
    PUNICODE_STRING pustrDevice,
    DEVMODEW *pdmInit,
    PUNICODE_STRING pustrLogAddr,
    ULONG iType,
    BOOL bDisplay,
    HANDLE hspool,
    VOID *pDriverInfo2,
    VOID *pUMdhpdev)
{
    PPDEVOBJ ppdev;
    PDC pdc;
    HDC hdc;

    DPRINT("GreOpenDCW(%S, iType=%ld)\n",
           pustrDevice ? pustrDevice->Buffer : NULL, iType);

    /* Get a PDEVOBJ for the device */
    ppdev = EngpGetPDEV(pustrDevice);
    if (!ppdev)
    {
        DPRINT1("Didn't find a suitable PDEV\n");
        return NULL;
    }

    DPRINT("GreOpenDCW - ppdev = %p\n", ppdev);

    pdc = DC_AllocDcWithHandle();
    if (!pdc)
    {
        DPRINT1("Could not Allocate a DC\n");
        PDEVOBJ_vRelease(ppdev);
        return NULL;
    }
    hdc = pdc->BaseObject.hHmgr;

    DC_vInitDc(pdc, iType, ppdev);
    /* FIXME: HACK! */
    DC_InitHack(pdc);

    DC_AllocDcAttr(pdc);

    DC_UnlockDc(pdc);

    DPRINT("returning hdc = %p\n", hdc);

    return hdc;
}

HDC
APIENTRY
NtGdiOpenDCW(
    PUNICODE_STRING pustrDevice,
    DEVMODEW *pdmInit,
    PUNICODE_STRING pustrLogAddr,
    ULONG iType,
    BOOL bDisplay,
    HANDLE hspool,
    VOID *pDriverInfo2,
    VOID *pUMdhpdev)
{
    UNICODE_STRING ustrDevice;
    WCHAR awcDevice[CCHDEVICENAME];
    DEVMODEW dmInit;
    PVOID dhpdev;
    HDC hdc;

    /* Only if a devicename is given, we need any data */
    if (pustrDevice)
    {
        /* Initialize destination string */
        RtlInitEmptyUnicodeString(&ustrDevice, awcDevice, sizeof(awcDevice));

        _SEH2_TRY
        {
            /* Probe the UNICODE_STRING and the buffer */
            ProbeForRead(pustrDevice, sizeof(UNICODE_STRING), 1);
            ProbeForRead(pustrDevice->Buffer, pustrDevice->Length, 1);

            /* Copy the string */
            RtlCopyUnicodeString(&ustrDevice, pustrDevice);

            if (pdmInit)
            {
                /* FIXME: could be larger */
                ProbeForRead(pdmInit, sizeof(DEVMODEW), 1);
                RtlCopyMemory(&dmInit, pdmInit, sizeof(DEVMODEW));
            }

            if (pUMdhpdev)
            {
                ProbeForWrite(pUMdhpdev, sizeof(HANDLE), 1);
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            SetLastNtError(_SEH2_GetExceptionCode());
            _SEH2_YIELD(return NULL);
        }
        _SEH2_END
    }
    else
    {
        pdmInit = NULL;
        pUMdhpdev = NULL;
    }

    /* FIXME: HACK! */
    if (pustrDevice)
    {
        UNICODE_STRING ustrDISPLAY = RTL_CONSTANT_STRING(L"DISPLAY");
        if (RtlEqualUnicodeString(&ustrDevice, &ustrDISPLAY, TRUE))
        {
            pustrDevice = NULL;
        }
    }

    /* Call the internal function */
    hdc = GreOpenDCW(pustrDevice ? &ustrDevice : NULL,
                     pdmInit ? &dmInit : NULL,
                     NULL, // fixme pwszLogAddress
                     iType,
                     bDisplay,
                     hspool,
                     NULL, //FIXME: pDriverInfo2
                     pUMdhpdev ? &dhpdev : NULL);

    /* If we got a HDC and a UM dhpdev is requested,... */
    if (hdc && pUMdhpdev)
    {
        /* Copy dhpdev to caller (FIXME: use dhpdev?? */
        _SEH2_TRY
        {
            /* Pointer was already probed */
            *(HANDLE*)pUMdhpdev = dhpdev;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Ignore error */
        }
        _SEH2_END
    }

    return hdc;
}


HDC
APIENTRY
NtGdiCreateCompatibleDC(HDC hdc)
{
    HDC hdcNew;
    PPDEVOBJ ppdev;
    PDC pdc, pdcNew;

    DPRINT("NtGdiCreateCompatibleDC(0x%p)\n", hdc);

    /* Did the caller provide a DC? */
    if (hdc)
    {
        /* Yes, try to lock it */
        pdc = DC_LockDc(hdc);
        if (!pdc)
        {
            DPRINT1("Could not lock source DC %p\n", hdc);
            return NULL;
        }

        /* Get the pdev from the DC */
        ppdev = pdc->ppdev;
        InterlockedIncrement(&ppdev->cPdevRefs);

        /* Unlock the source DC */
        DC_UnlockDc(pdc);
    }
    else
    {
        /* No DC given, get default device */
        ppdev = EngpGetPDEV(NULL);
    }

    if (!ppdev)
    {
        DPRINT1("Didn't find a suitable PDEV\n");
        return NULL;
    }

    /* Allocate a new DC */
    pdcNew = DC_AllocDcWithHandle();
    if (!pdcNew)
    {
        DPRINT1("Could not allocate a new DC\n");
        PDEVOBJ_vRelease(ppdev);
        return NULL;
    }
    hdcNew = pdcNew->BaseObject.hHmgr;

    /* Initialize the new DC */
    DC_vInitDc(pdcNew, DCTYPE_MEMORY, ppdev);
    /* FIXME: HACK! */
    DC_InitHack(pdcNew);

    /* Allocate a dc attribute */
    DC_AllocDcAttr(pdcNew);

    PDEVOBJ_vRelease(ppdev);

    // HACK!
    DC_vSelectSurface(pdcNew, psurfDefaultBitmap);

    DC_UnlockDc(pdcNew);

    DPRINT("Leave NtGdiCreateCompatibleDC hdcNew = %p\n", hdcNew);

    return hdcNew;
}

BOOL
FASTCALL
IntGdiDeleteDC(HDC hDC, BOOL Force)
{
    PDC DCToDelete = DC_LockDc(hDC);

    if (DCToDelete == NULL)
    {
        SetLastWin32Error(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (!Force)
    {
        if (DCToDelete->fs & DC_FLAG_PERMANENT)
        {
            DPRINT1("No! You Naughty Application!\n");
            DC_UnlockDc(DCToDelete);
            return UserReleaseDC(NULL, hDC, FALSE);
        }
    }

    DC_UnlockDc(DCToDelete);

    if (!IsObjectDead(hDC))
    {
        if (!GDIOBJ_FreeObjByHandle(hDC, GDI_OBJECT_TYPE_DC))
        {
            DPRINT1("DC_FreeDC failed\n");
        }
    }
    else
    {
        DPRINT1("Attempted to Delete 0x%x currently being destroyed!!!\n", hDC);
    }

    return TRUE;
}

BOOL
APIENTRY
NtGdiDeleteObjectApp(HANDLE DCHandle)
{
    /* Complete all pending operations */
    NtGdiFlushUserBatch();

    if (GDI_HANDLE_IS_STOCKOBJ(DCHandle)) return TRUE;

    if (GDI_HANDLE_GET_TYPE(DCHandle) != GDI_OBJECT_TYPE_DC)
        return GreDeleteObject((HGDIOBJ) DCHandle);

    if (IsObjectDead((HGDIOBJ)DCHandle)) return TRUE;

    if (!GDIOBJ_OwnedByCurrentProcess(DCHandle))
    {
        SetLastWin32Error(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return IntGdiDeleteDC(DCHandle, FALSE);
}

BOOL
APIENTRY
NtGdiMakeInfoDC(
    IN HDC hdc,
    IN BOOL bSet)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return FALSE;
}


HDC FASTCALL
IntGdiCreateDC(
    PUNICODE_STRING Driver,
    PUNICODE_STRING pustrDevice,
    PVOID pUMdhpdev,
    CONST PDEVMODEW pdmInit,
    BOOL CreateAsIC)
{
    HDC hdc;

    hdc = GreOpenDCW(pustrDevice,
                     pdmInit,
                     NULL,
                     CreateAsIC ? DCTYPE_INFO :
                          (Driver ? DC_TYPE_DIRECT : DC_TYPE_DIRECT),
                     TRUE,
                     NULL,
                     NULL,
                     pUMdhpdev);

    return hdc;
}

HDC FASTCALL
IntGdiCreateDisplayDC(HDEV hDev, ULONG DcType, BOOL EmptyDC)
{
    HDC hDC;
    UNIMPLEMENTED;
    ASSERT(FALSE);

    if (DcType == DC_TYPE_MEMORY)
        hDC = NtGdiCreateCompatibleDC(NULL); // OH~ Yuck! I think I taste vomit in my mouth!
    else
        hDC = IntGdiCreateDC(NULL, NULL, NULL, NULL, (DcType == DC_TYPE_INFO));

    return hDC;
}

