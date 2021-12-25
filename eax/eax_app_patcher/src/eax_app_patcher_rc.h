/*

EAX OpenAL Extension

Copyright (c) 2020-2021 Boris I. Bendovsky (bibendovsky@hotmail.com) and Contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

*/


#define VS_VERSION_INFO 1

#ifndef VS_FFI_FILEFLAGSMASK
#define VS_FFI_FILEFLAGSMASK 0x0000003FL
#endif // !VS_FFI_FILEFLAGSMASK

#ifdef _DEBUG
#define VER_DBG 1
#else // _DEBUG
#define VER_DBG 0
#endif // _DEBUG

#ifndef VOS_NT
#define VOS_NT 0x00040000L
#endif // !VOS_NT

#ifndef VFT_DRV
#define VFT_DRV 0x00000003L
#endif // !VFT_DRV

#ifndef VFT2_DRV_SYSTEM
#define VFT2_DRV_SYSTEM 0x00000007L
#endif // !VFT2_DRV_SYSTEM
