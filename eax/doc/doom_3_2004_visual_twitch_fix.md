Doom 3 visual twitch fix
========================


Referenced source file:
http://github.com/id-Software/DOOM-3/blob/master/neo/sound/snd_cache.cpp


1. Patch block #1  
`snd_cache.cpp:501`

```
...
499:				if ( alGetError() != AL_NO_ERROR ) {
500:					common->Error( "idSoundCache: error loading data into OpenAL hardware buffer" );
501:				} else {
...:
524:					hardwareBuffer = true;
525:				}
...
```

```
004EAF3F  74 18                jz      l_4EAF59
004EAF41  A1 A4 FF 7C 00       mov     eax, o_7CFFA4
004EAF46  8B 08                mov     ecx, [eax]
004EAF48  68 E4 34 77 00       push    offset "idSoundCache: error loading data into OpenAL hardware buffer"
004EAF4D  50                   push    eax
004EAF4E  FF 51 5C             call    dword ptr [ecx + 5Ch]
004EAF51  83 C4 08             add     esp, 8
004EAF54  E9 C5 00 00 00       jmp     l_4EB01E
```


FIX
---
Skip to `"hardwareBuffer = true;"`.
```
004EAF3F  0F 84 CE 00 00 00    jz      l_4EB013
```


2. Patch block #2  
snd_cache.cpp:581

```
...
579:					if ( alGetError() != AL_NO_ERROR )
580:						common->Error( "idSoundCache: error loading data into OpenAL hardware buffer" );
581:					else {
...
604:						hardwareBuffer = true;
605:					}
...
```

```
004EB270  74 18                jz      l_4EB28A
004EB272  A1 A4 FF 7C 00       mov     eax, o_7CFFA4
004EB277  8B 08                mov     ecx, [eax]
004EB279  68 E4 34 77 00       push    offset "idSoundCache: error loading data into OpenAL hardware buffer"
004EB27E  50                   push    eax
004EB27F  FF 51 5C             call    dword ptr [ecx + 5Ch]
004EB282  83 C4 08             add     esp, 8
004EB285  E9 EE 00 00 00       jmp     l_4EB378
```


FIX
---
Skip to `"hardwareBuffer = true;"`.
```
004EB270  0F 84 F6 00 00 00    jz      l_4EB36C
```


3. Patch block #3  
snd_cache.cpp:614

```
...
613:		// Free memory if sample was loaded into hardware
614:		if ( hardwareBuffer ) {
615:			soundCacheAllocator.Free( nonCacheData );
616:			nonCacheData = NULL;
617:		}
...
```

```
004EB38F  8B 44 24 10          mov     eax, [esp + 0B0h + var_A0]
004EB393  8A 48 4C             mov     cl, [eax + 4Ch]
004EB396  84 C9                test    cl, cl
004EB398  5D                   pop     ebp
004EB399  5B                   pop     ebx
004EB39A  74 19                jz      l_4EB3B5
```

FIX
---
Skip whole `if`.
```
004EB39A  EB 19                jmp     loc_4EB3B5
```
