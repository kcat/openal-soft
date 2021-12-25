Prey (2006) source occlusion out of range fix
=============================================

Clamp occlusion value before calling `EAXSet`.  
Occlusion value is first field inside `EAXOCCLUSIONPROPERTIES` structure.  
Since clamping code does not fit at `00512197` and `0051217e` we must do it somewhere else.  
A good place is region at the end of `.text` segment (`007a8c48`) filled with zeros.



Unpatched block #1
------------------
```
0051218c 75 1c                JNZ        LAB_005121aa
0051218e 8b 4d 7c             MOV        ECX,dword ptr [EBP + 0x7c]
00512191 6a 10                PUSH       0x10                       // Property size (size of EAXOCCLUSIONPROPERTIES).
00512193 8d 54 24 54          LEA        EDX,[ESP + 0x54]
00512197 52                   PUSH       EDX                        // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
00512198 51                   PUSH       ECX                        // Property AL name
00512199 6a 03                PUSH       0x3                        // Property id (EAXSOURCE_OCCLUSIONPARAMETERS).
0051219b 68 b0 26 93 00       PUSH       DAT_009326b0               // Property set GUID (EAXPROPERTYID_EAX40_Source).
005121a0 ff d0                CALL       EAX                        // Call EAXSet.
```

Unpatched block #2
------------------
```
00512173 75 14                JNZ        LAB_00512189
00512175 8b 4d 7c             MOV        ECX,dword ptr [EBP + 0x7c]
00512178 6a 10                PUSH       0x10                       // Property size (size of EAXOCCLUSIONPROPERTIES).
0051217a 8d 54 24 54          LEA        EDX,[ESP + 0x54]
0051217e 52                   PUSH       EDX                        // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
0051217f 51                   PUSH       ECX                        // Property AL name.
00512180 6a 03                PUSH       0x3                        // Property id (EAXSOURCE_OCCLUSIONPARAMETERS).
00512182 68 10 27 93 00       PUSH       DAT_00932710               // Property set GUID (EAXPROPERTYID_EAX50_Source).
00512187 eb 17                JMP        LAB_005121a0               // Go to call to EAXSet in the block #1.
```

Unpatched block #3
----------------
```
007a8c48 00
...
...
007a8c7f 00

(total 56 bytes)
```


Patched block #1
----------------
```
0051218c 75 1c                JNZ        LAB_005121aa
0051218e 8b 4d 7c             MOV        ECX,dword ptr [EBP + 0x7c]
00512191 6a 10                PUSH       0x10
00512193 8d 54 24 54          LEA        EDX,[ESP + 0x54]
00512197 e9 ac 6a 29 00       JMP        LAB_007a8c48               // Go to piece #1 of block #3.
0051219c b0                   ??
0051219d 26                   ??
0051219e 93                   ??
0051219f 00                   ??
                              LAB_5121a0:                           // Back here from both pieces of block #3.
005121a0 ff d0                CALL       EAX                        // Call EAXSet
```

Patched block #2
----------------
```
00512173 75 14                JNZ        LAB_00512189
00512175 8b 4d 7c             MOV        ECX,dword ptr [EBP + 0x7c]
00512178 6a 10                PUSH       0x10
0051217a 8d 54 24 54          LEA        EDX,[ESP + 0x54]
0051217e e9 e1 6a 29 00       JMP        LAB_007a8c64               // Go to piece #2 of block #3.
00512183 10                   ??
00512184 27                   ??
00512185 93                   ??
00512186 00                   ??
00512187 eb 17                JMP        LAB_5121a0
```


Patched block #3
----------------
```
                              LAB_007a8c48:
007a8c48 81 3a f0 d8 ff ff    CMP        dword ptr [EDX],0xffffd8f0
007a8c4e 7d 06                JGE        LAB_007a8c56               // Is occlusion less than or equal to `-10000`?
007a8c50 c7 02 f0 d8 ff ff    MOV        dword ptr [EDX],0xffffd8f0 // No. Set minimum value (-10000).
                              LAB_007a8c56:
007a8c56 52                   PUSH       EDX                        // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
007a8c57 51                   PUSH       ECX                        // Property AL name.
007a8c58 6a 03                PUSH       0x3                        // Property id (EAXSOURCE_OCCLUSIONPARAMETERS).
007a8c5a 68 b0 26 93 00       PUSH       0x9326b0                   // Property set GUID (EAXPROPERTYID_EAX40_Source).
007a8c5f e9 3c 95 d6 ff       JMP        LAB_5121a0

                              LAB_007a8c64:
007a8c64 81 3a f0 d8 ff ff    CMP        dword ptr [EDX],0xffffd8f0
007a8c6a 7d 06                JGE        LAB_007a8c72               // Is occlusion less than or equal to `-10000`?
007a8c6c c7 02 f0 d8 ff ff    MOV        dword ptr [EDX],0xffffd8f0 // No. Set minimum value (-10000).
                              LAB_007a8c72:
007a8c72 52                   PUSH       EDX                        // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
007a8c73 51                   PUSH       ECX                        // Property AL name.
007a8c74 6a 03                PUSH       0x3                        // Property id (EAXSOURCE_OCCLUSIONPARAMETERS).
007a8c76 68 10 27 93 00       PUSH       0x932710                   // Property set GUID (EAXPROPERTYID_EAX50_Source).
007a8c7b e9 20 95 d6 ff       JMP        LAB_5121a0
```
