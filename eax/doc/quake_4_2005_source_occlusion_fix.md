Quake 4 (2005) source occlusion out of range fix
================================================

Clamp occlusion value before calling `EAXSet`.  
Occlusion value is first field inside `EAXOCCLUSIONPROPERTIES` structure.  
Since clamping code does not fit at `101739e8` and `101739c7` we must do it somewhere else.  
A good place is region at the end of `.text` segment (`102af864`) filled with zeros.


Unpatched block #1
------------------
```
101739dd 75 17               JNZ        LAB_101739f6
101739df 8b 4d 78            MOV        ECX,dword ptr [EBP + 0x78]
101739e2 6a 10               PUSH       0x10                       // Property size (size of EAXOCCLUSIONPROPERTIES).
101739e4 8d 54 24 58         LEA        EDX,[ESP + 0x58]
101739e8 52                  PUSH       EDX                        // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
101739e9 51                  PUSH       ECX                        // Property AL name.
101739ea 6a 03               PUSH       0x3                        // Property id (EAXSOURCE_OCCLUSIONPARAMETERS).
101739ec 68 80 de 3f 10      PUSH       DAT_103fde80               // Property set GUID (EAXPROPERTYID_EAX40_Source).
                             LAB_101739f1:
101739f1 ff d0               CALL       EAX                        // Call EAXSet.
```

Unpatched block #2
------------------
```
101739bc 75 14               JNZ        LAB_101739d2
101739be 8b 4d 78            MOV        ECX,dword ptr [EBP + 0x78]
101739c1 6a 10               PUSH       0x10                       // Property size (size of EAXOCCLUSIONPROPERTIES).
101739c3 8d 54 24 58         LEA        EDX,[ESP + 0x58]
101739c7 52                  PUSH       EDX                        // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
101739c8 51                  PUSH       ECX                        // Property AL name.
101739c9 6a 03               PUSH       0x3                        // Property id (EAXSOURCE_OCCLUSIONPARAMETERS).
101739cb 68 e0 de 3f 10      PUSH       DAT_103fdee0               // Property set GUID (EAXPROPERTYID_EAX50_Source).
101739d0 eb 1f               JMP        LAB_101739f1               // Go to call to EAXSet in the block #1.
```

Unpatched block #3
------------------
```
102af864 00
...
...
102af89b 00

(56 bytes)
```


Patched block #1
----------------
```
101739dd 75 17               JNZ        LAB_101739f6
101739df 8b 4d 78            MOV        ECX,dword ptr [EBP + 0x78]
101739e2 6a 10               PUSH       0x10                       // Property size (size of EAXOCCLUSIONPROPERTIES).
101739e4 8d 54 24 58         LEA        EDX,[ESP + 0x58]           // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
101739e8 e9 77 be 13 00      JMP        LAB_102af864               // Go to piece #1 of block #3.
101739ed 80                  ??
101739ee de                  ??
101739ef 3f                  ??
101739f0 10                  ??
                             LAB_101739f1:                         // Back here from both pieces of block #3
101739f1 ff d0               CALL       EAX                        // Call EAXSet
```

Patched block #2
----------------
```
101739bc 75 14               JNZ        LAB_101739d2
101739be 8b 4d 78            MOV        ECX,dword ptr [EBP + 0x78]
101739c1 6a 10               PUSH       0x10                       // Property size (size of EAXOCCLUSIONPROPERTIES).
101739c3 8d 54 24 58         LEA        EDX,[ESP + 0x58]           // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
101739c7 e9 b4 be 13 00      JMP        LAB_102af880               // Go to piece #2 of block #3.
101739cc e0                  ??
101739cd de                  ??
101739ce 3f                  ??
101739cf 10                  ??
101739d0 eb 1f               JMP        LAB_101739f1
```

Patched block #3
----------------
```
                              LAB_102af864:
102af864 81 3a f0 d8 ff ff    CMP        dword ptr [EDX],0xffffd8f0
102af86a 7d 06                JGE        LAB_102af872               // Is occlusion less than or equal to `-10000`?
102af86c c7 02 f0 d8 ff ff    MOV        dword ptr [EDX],0xffffd8f0 // No. Set minimum value (-10000).
                              LAB_102af872:
102af872 52                   PUSH       EDX                        // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
102af873 51                   PUSH       ECX                        // Property AL name.
102af874 6a 03                PUSH       0x3                        // Property id (EAXSOURCE_OCCLUSIONPARAMETERS).
102af876 68 80 de 3f 10       PUSH       0x103fde80                 // Property set GUID (EAXPROPERTYID_EAX40_Source).
102af87b e9 71 41 ec ff       JMP        LAB_101739f1

                              LAB_102af880:
102af880 81 3a f0 d8 ff ff    CMP        dword ptr [EDX],0xffffd8f0
102af886 7d 06                JGE        LAB_102af88e               // Is occlusion less than or equal to `-10000`?
102af888 c7 02 f0 d8 ff ff    MOV        dword ptr [EDX],0xffffd8f0 // No. Set minimum value (-10000).
                              LAB_102af88e:
102af88e 52                   PUSH       EDX                        // Property buffer (pointer to EAXOCCLUSIONPROPERTIES).
102af88f 51                   PUSH       ECX                        // Property AL name.
102af890 6a 03                PUSH       0x3                        // Property id (EAXSOURCE_OCCLUSIONPARAMETERS).
102af892 68 e0 de 3f 10       PUSH       0x103fdee0                 // Property set GUID (EAXPROPERTYID_EAX50_Source).
102af897 e9 55 41 ec ff       JMP        LAB_101739f1
```
