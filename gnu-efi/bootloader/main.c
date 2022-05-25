#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <elf.h>
#include "config.h"


// 2022 Ian Moffett.


struct __attribute__((packed)) BMP {
    struct __attribute__((packed)) Header {
        uint16_t signature;                     // 'BM'.
        uint32_t file_size;
        uint32_t reserved;
        uint32_t data_offset;
    } header;

    struct __attribute__((packed)) InfoHeader {
        uint32_t size;              // Sizeof info_header.
        uint32_t width;             // Width of bitmap in pixels.
        uint32_t height;            // Height of bitmap in pixels.
        uint16_t nplanes;
        uint16_t bits_per_pixel;
        uint32_t compression;
        uint32_t image_size;
        uint32_t xpixels_per_meter;
        uint32_t ypixels_per_meter;
        uint32_t colors_used;
        uint32_t important_colors;
    } info_header;

    struct __attribute__((packed)) ColorTable {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        uint8_t reserved;
    } color_table;

    uint8_t pixel_data[];
};


struct RuntimeDataAndServices {
    struct Framebuffer {
        void* base_addr;
        size_t buffer_size;
        unsigned int width;
        unsigned int height;
        unsigned int ppsl;          // Pixels per scanline.
    } framebuffer_data;
} runtime_services;


// Sets up graphics output protocol.
void init_gop(void) {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3, &gop_guid, NULL, (void**)&gop);

    // Abort if error.
    if (EFI_ERROR(status))
        return;
    
    runtime_services.framebuffer_data.base_addr = (void*)gop->Mode->FrameBufferBase;
    runtime_services.framebuffer_data.buffer_size = gop->Mode->FrameBufferSize;
    runtime_services.framebuffer_data.width = gop->Mode->Info->HorizontalResolution;
    runtime_services.framebuffer_data.height = gop->Mode->Info->VerticalResolution;
    runtime_services.framebuffer_data.ppsl = gop->Mode->Info->PixelsPerScanLine;
}



EFI_FILE_HANDLE get_volume(EFI_HANDLE image) {
  EFI_LOADED_IMAGE *loaded_image = NULL;                  // Image interface
  EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;      // Image interface GUID 
  EFI_FILE_IO_INTERFACE *IOVolume;                        // File system interface 
  EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID; // File system interface GUID 
  EFI_FILE_HANDLE Volume;                                 // The volume's interface 
 
  /* get the loaded image protocol interface for our "image" */
  uefi_call_wrapper(BS->HandleProtocol, 3, image, &lipGuid, (void **) &loaded_image);
  /* get the volume handle */
  uefi_call_wrapper(BS->HandleProtocol, 3, loaded_image->DeviceHandle, &fsGuid, (VOID*)&IOVolume);
  uefi_call_wrapper(IOVolume->OpenVolume, 2, IOVolume, &Volume);
  return Volume;
}


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


UINT64 getFileSize(EFI_HANDLE imageHandle, CHAR16* path) {
    EFI_FILE_HANDLE fileHandle;
    EFI_FILE_HANDLE volume = get_volume(imageHandle);
    uefi_call_wrapper(volume->Open, 5, volume, &fileHandle, path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    UINT64 ret;
    EFI_FILE_INFO* fileinfo = LibFileInfo(fileHandle);
    ret = fileinfo->FileSize;
    FreePool(fileinfo);                 // Free memory.
    return ret;
}


struct BMP* load_wallpaper(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* sysTable) {
    struct BMP* bmp = NULL;

    UINTN read_size = getFileSize(imageHandle, WALLPAPER_PATH);
    EFI_FILE* bmp_file_handle = load_file(NULL, WALLPAPER_PATH, imageHandle, sysTable);
    sysTable->BootServices->AllocatePool(EfiLoaderData, read_size, (void**)&bmp);
    
    // Check if failure to allocate memory.
    if (bmp == NULL) return NULL;

    bmp_file_handle->Read(bmp_file_handle, &read_size, bmp);
    return bmp;
}


void read_wallpaper_data(struct BMP* bmp) {
#if VERBOSE
    Print(L"Wallpaper filesize is %d Bytes.\n", bmp->header.file_size);
    Print(L"Wallpaper width is %d pixels.\n", bmp->info_header.width);
    Print(L"Wallpaper height is %d pixels.\n", bmp->info_header.height);
#endif
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
    init_gop();

#if USE_WALLPAPER
    struct BMP* wallpaper = load_wallpaper(imageHandle, sysTable);
    if (!(wallpaper)) {
        Print(L"Could not load wallpaper!\n");
    } else {
        // Verify that the signature is equal to 'BM'.
        if ((wallpaper->header.signature & 0xFF) == 'B' && (wallpaper->header.signature >> 8) == 'M') {
            read_wallpaper_data(wallpaper);               // Dump data if verbose mode is on.
        }
    }
#endif

    __asm__ __volatile__("cli; hlt");

    return EFI_SUCCESS;
}
