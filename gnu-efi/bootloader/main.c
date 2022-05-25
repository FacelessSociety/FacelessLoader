#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <elf.h>
#include "config.h"


// Menu entries.
const char* entries[10] = ENTRIES;


EFI_FILE* load_file(EFI_FILE* directory, CHAR16* path, EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* sysTable) {
    EFI_LOADED_IMAGE_PROTOCOL* loaded_img_protocol = NULL;
    EFI_FILE* fileres;
    EFI_STATUS status;      // Just a status var.

    // Get protocols.
    sysTable->BootServices->HandleProtocol(imageHandle, &gEfiLoadedImageProtocolGuid, (void**)&loaded_img_protocol);
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem_protocol;
    sysTable->BootServices->HandleProtocol(loaded_img_protocol->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&filesystem_protocol);

    filesystem_protocol->OpenVolume(filesystem_protocol, &directory);

    // Open up file.
    status = directory->Open(directory, &fileres, path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);

    // Could not open the file.
    if (status != EFI_SUCCESS) return NULL;
    return fileres;
}



/*
 *  This is our entry point.
 *
 *  Here are some thing to know:
 *
 *  Handles are a collection of one or more protocols.
 *  Protocols are data structures named by a GUID.
 *  The data structure for a protocol may contain 
 *  data fields, services, both or none at all.
 *
 */

EFI_STATUS efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* sysTable) {
    InitializeLib(imageHandle, sysTable);

    __asm__ __volatile__("cli; hlt");
    return EFI_SUCCESS;
}
