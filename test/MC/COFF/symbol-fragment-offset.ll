; The purpose of this test is to see if the COFF object writer is emitting the
; proper relocations for multiple pieces of data in a single data fragment.

; RUN: llc -filetype=obj -mtriple i686-pc-win32 %s -o %t
; RUN: coff-dump.py %abs_tmp | FileCheck %s
; RUN: llc -filetype=obj -mtriple x86_64-pc-win32 %s -o %t

@.str = private constant [7 x i8] c"Hello \00"    ; <[7 x i8]*> [#uses=1]
@str = internal constant [7 x i8] c"World!\00"    ; <[7 x i8]*> [#uses=1]

define i32 @main() nounwind {
entry:
  %call = tail call i32 (i8*, ...)* @printf(i8* getelementptr inbounds ([7 x i8]* @.str, i32 0, i32 0)) nounwind ; <i32> [#uses=0]
  %puts = tail call i32 @puts(i8* getelementptr inbounds ([7 x i8]* @str, i32 0, i32 0)) ; <i32> [#uses=0]
  ret i32 0
}

declare i32 @printf(i8* nocapture, ...) nounwind

declare i32 @puts(i8* nocapture) nounwind

; CHECK: {
; CHECK:   MachineType              = IMAGE_FILE_MACHINE_I386 (0x14C)
; CHECK:   NumberOfSections         = 2
; CHECK:   TimeDateStamp            = {{[0-9]+}}
; CHECK:   PointerToSymbolTable     = 0x{{[0-9A-F]+}}
; CHECK:   NumberOfSymbols          = 8
; CHECK:   SizeOfOptionalHeader     = 0
; CHECK:   Characteristics          = 0x0
; CHECK:   Sections                 = [
; CHECK:     1 = {
; CHECK:       Name                     = .text
; CHECK:       VirtualSize              = 0
; CHECK:       VirtualAddress           = 0
; CHECK:       SizeOfRawData            = {{[0-9]+}}
; CHECK:       PointerToRawData         = 0x{{[0-9A-F]+}}
; CHECK:       PointerToRelocations     = 0x{{[0-9A-F]+}}
; CHECK:       PointerToLineNumbers     = 0x0
; CHECK:       NumberOfRelocations      = 4
; CHECK:       NumberOfLineNumbers      = 0
; CHECK:       Charateristics           = 0x60500020
; CHECK:         IMAGE_SCN_CNT_CODE
; CHECK:         IMAGE_SCN_ALIGN_16BYTES
; CHECK:         IMAGE_SCN_MEM_EXECUTE
; CHECK:         IMAGE_SCN_MEM_READ
; CHECK:       SectionData              =
; CHECK:       Relocations              = [
; CHECK:         0 = {
; CHECK:           VirtualAddress           = 0x{{[0-9A-F]+}}
; CHECK:           SymbolTableIndex         = 2
; CHECK:           Type                     = IMAGE_REL_I386_DIR32 (6)
; CHECK:           SymbolName               =
; CHECK:         }
; CHECK:         1 = {
; CHECK:           VirtualAddress           = 0x{{[0-9A-F]+}}
; CHECK:           SymbolTableIndex         = 5
; CHECK:           Type                     = IMAGE_REL_I386_REL32 (20)
; CHECK:           SymbolName               = _main
; CHECK:         }
; CHECK:         2 = {
; CHECK:           VirtualAddress           = 0x{{[0-9A-F]+}}
; CHECK:           SymbolTableIndex         = 6
; CHECK:           Type                     = IMAGE_REL_I386_DIR32 (6)
; CHECK:           SymbolName               = _printf
; CHECK:         }
; CHECK:         3 = {
; CHECK:           VirtualAddress           = 0x{{[0-9A-F]+}}
; CHECK:           SymbolTableIndex         = 7
; CHECK:           Type                     = IMAGE_REL_I386_REL32 (20)
; CHECK:           SymbolName               = _str
; CHECK:         }
; CHECK:       ]
; CHECK:     }
; CHECK:     2 = {
; CHECK:       Name                     = .data
; CHECK:       VirtualSize              = 0
; CHECK:       VirtualAddress           = 0
; CHECK:       SizeOfRawData            = {{[0-9]+}}
; CHECK:       PointerToRawData         = 0x{{[0-9A-F]+}}
; CHECK:       PointerToRelocations     = 0x0
; CHECK:       PointerToLineNumbers     = 0x0
; CHECK:       NumberOfRelocations      = 0
; CHECK:       NumberOfLineNumbers      = 0
; CHECK:       Charateristics           = 0xC0100040
; CHECK:         IMAGE_SCN_CNT_INITIALIZED_DATA
; CHECK:         IMAGE_SCN_ALIGN_1BYTES
; CHECK:         IMAGE_SCN_MEM_READ
; CHECK:         IMAGE_SCN_MEM_WRITE
; CHECK:       SectionData              =
; CHECK:         48 65 6C 6C 6F 20 00 57 - 6F 72 6C 64 21 00       |Hello .World!.|

; CHECK:       Relocations              = None
; CHECK:     }
; CHECK:   ]
; CHECK:   Symbols                  = [
; CHECK:     0 = {
; CHECK:       Name                     = .text
; CHECK:       Value                    = 0
; CHECK:       SectionNumber            = 1
; CHECK:       SimpleType               = IMAGE_SYM_TYPE_NULL (0)
; CHECK:       ComplexType              = IMAGE_SYM_DTYPE_NULL (0)
; CHECK:       StorageClass             = IMAGE_SYM_CLASS_STATIC (3)
; CHECK:       NumberOfAuxSymbols       = 1
; CHECK:       AuxillaryData            =
; CHECK:         21 00 00 00 04 00 00 00 - 00 00 00 00 01 00 00 00 |!...............|
; CHECK:         00 00                                             |..|

; CHECK:     }
; CHECK:     1 = {
; CHECK:       Name                     = .data
; CHECK:       Value                    = 0
; CHECK:       SectionNumber            = 2
; CHECK:       SimpleType               = IMAGE_SYM_TYPE_NULL (0)
; CHECK:       ComplexType              = IMAGE_SYM_DTYPE_NULL (0)
; CHECK:       StorageClass             = IMAGE_SYM_CLASS_STATIC (3)
; CHECK:       NumberOfAuxSymbols       = 1
; CHECK:       AuxillaryData            =
; CHECK:         0E 00 00 00 00 00 00 00 - 00 00 00 00 02 00 00 00 |................|
; CHECK:         00 00                                             |..|

; CHECK:     }
; CHECK:     2 = {
; CHECK:       Name                     = _main
; CHECK:       Value                    = 0
; CHECK:       SectionNumber            = 1
; CHECK:       SimpleType               = IMAGE_SYM_TYPE_NULL (0)
; CHECK:       ComplexType              = IMAGE_SYM_DTYPE_FUNCTION (2)
; CHECK:       StorageClass             = IMAGE_SYM_CLASS_EXTERNAL (2)
; CHECK:       NumberOfAuxSymbols       = 0
; CHECK:       AuxillaryData            =

; CHECK:     3 = {
; CHECK:       Name                     = _printf
; CHECK:       Value                    = 0
; CHECK:       SectionNumber            = 0
; CHECK:       SimpleType               = IMAGE_SYM_TYPE_NULL (0)
; CHECK:       ComplexType              = IMAGE_SYM_DTYPE_NULL (0)
; CHECK:       StorageClass             = IMAGE_SYM_CLASS_EXTERNAL (2)
; CHECK:       NumberOfAuxSymbols       = 0
; CHECK:       AuxillaryData            =

; CHECK:     }
; CHECK:     4 = {
; CHECK:       Name                     = _str
; CHECK:       Value                    = 7
; CHECK:       SectionNumber            = 2
; CHECK:       SimpleType               = IMAGE_SYM_TYPE_NULL (0)
; CHECK:       ComplexType              = IMAGE_SYM_DTYPE_NULL (0)
; CHECK:       StorageClass             = IMAGE_SYM_CLASS_STATIC (3)
; CHECK:       NumberOfAuxSymbols       = 0
; CHECK:       AuxillaryData            =

; CHECK:     }
; CHECK:     5 = {
; CHECK:       Name                     = _puts
; CHECK:       Value                    = 0
; CHECK:       SectionNumber            = 0
; CHECK:       SimpleType               = IMAGE_SYM_TYPE_NULL (0)
; CHECK:       ComplexType              = IMAGE_SYM_DTYPE_NULL (0)
; CHECK:       StorageClass             = IMAGE_SYM_CLASS_EXTERNAL (2)
; CHECK:       NumberOfAuxSymbols       = 0
; CHECK:       AuxillaryData            =

; CHECK:     }
; CHECK:   ]
; CHECK: }
