@echo off
rem Build TDU_DealerOwnedGreen.asi with MSVC -- MUST be 32-bit (the game is x86).
rem Run from an "x86 Native Tools Command Prompt for VS" (or call vcvars32.bat first).
cl /nologo /LD /O2 /MT /GS- /W3 TDU_DealerOwnedGreen.c /link /MACHINE:X86 /SUBSYSTEM:WINDOWS /SAFESEH:NO /OUT:TDU_DealerOwnedGreen.asi kernel32.lib
