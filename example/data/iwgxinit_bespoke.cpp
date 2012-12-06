/*
 * Copyright (C) 2001-2008 Ideaworks3D Ltd.
 * All Rights Reserved.
 *
 * This document is protected by copyright, and contains information
 * proprietary to Ideaworks3D Ltd.
 * This file consists of source code released by Ideaworks3D Ltd under
 * the terms of the accompanying End User License Agreement (EULA).
 * Please do not use this program/source code before you have read the
 * EULA and have agreed to be bound by its terms.
 */

//-----------------------------------------------------------------------------
/*!
	\file IwGxInit_Bespoke.cpp
	\brief Auto-generated file which links in only the pixel drawing loops used previously.
*/
//-----------------------------------------------------------------------------
// AUTO-GENERATED File. Do Not Edit.
//This file is generated in IwGxTerminate() using the data recorded in IwGxScanloopUsage.bin
//

#include "s3eTypes.h"
// self contained forward defines
typedef struct _scanLine ScanLine;

typedef void (*IwGxScanFunc)(ScanLine* );
extern void IwRendInitScanFunc(uint32 , IwGxScanFunc );
extern void _IwGxFinishSWInit();

extern int g_RendInitType;

// externs for all the functions mentioned below
extern void IwRendConnectStandard();
extern void IwRend_EnableEnvMap();
typedef void (*IwPolyRendrFunc)(uint32 *);
extern void SetLinkPipe(int32 , IwPolyRendrFunc );

//-------------------------------------------------------------
void IwGxInit_Bespoke()
{

    IwRendConnectStandard();
    _IwGxFinishSWInit();
    IwRend_EnableEnvMap();
    g_RendInitType = 45;
}
