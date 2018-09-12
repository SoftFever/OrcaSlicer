/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2006 Christian Starkjohann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* $Id$ */

/*
The following is a replacement for hidsdi.h from the Windows DDK. It defines some
of the types and function prototypes of this header for our project. If you
have the Windows DDK version of this file or a version shipped with MinGW, use
that instead.
*/
#ifndef MY_DDK_HIDSDI_H
#define MY_DDK_HIDSDI_H
#include <pshpack4.h>
#include <ddk/hidusage.h>
#include <ddk/hidpi.h>
typedef struct{
    ULONG   Size;
    USHORT  VendorID;
    USHORT  ProductID;
    USHORT  VersionNumber;
}HIDD_ATTRIBUTES;
void __stdcall      HidD_GetHidGuid(OUT LPGUID hidGuid);
BOOLEAN __stdcall   HidD_GetAttributes(IN HANDLE device, OUT HIDD_ATTRIBUTES *attributes);
BOOLEAN __stdcall   HidD_GetManufacturerString(IN HANDLE device, OUT void *buffer, IN ULONG bufferLen);
BOOLEAN __stdcall   HidD_GetProductString(IN HANDLE device, OUT void *buffer, IN ULONG bufferLen);
BOOLEAN __stdcall   HidD_GetSerialNumberString(IN HANDLE device, OUT void *buffer, IN ULONG bufferLen);
BOOLEAN __stdcall   HidD_GetFeature(IN HANDLE device, OUT void *reportBuffer, IN ULONG bufferLen);
BOOLEAN __stdcall   HidD_SetFeature(IN HANDLE device, IN void *reportBuffer, IN ULONG bufferLen);
BOOLEAN __stdcall   HidD_GetNumInputBuffers(IN HANDLE device, OUT ULONG *numBuffers);
BOOLEAN __stdcall   HidD_SetNumInputBuffers(IN HANDLE device, OUT ULONG numBuffers);
#include <poppack.h>
#endif /* MY_DDK_HIDSDI_H */
