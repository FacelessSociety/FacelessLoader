#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <elf.h>
#include "config.h"


// 2022 Ian Moffett.
// 2022 TJ

#define BLEND_GET_ALPHA(color) ((color >> 24) & 0x000000FF)
#define BLEND_GET_RED(color)   ((color >> 16)   & 0x000000FF)
#define BLEND_GET_GREEN(color) ((color >> 8)  & 0x000000FF)
#define BLEND_GET_BLUE(color)  ((color >> 0)   & 0X000000FF)


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

    char pixel_data[];
};

struct RuntimeDataAndServices {
    struct Framebuffer {
        void* base_addr;
        size_t buffer_size;
        unsigned int width;
        unsigned int height;
        unsigned int ppsl;          // Pixels per scanline.
    } framebuffer_data;

    struct MemoryMap {
        EFI_MEMORY_DESCRIPTOR* map;
        UINTN mapSize;
        UINTN mapDescSize;
    } mmap;
    
    struct BMP* wallpaper;

    void(*display_wallpaper)(void);
    void(*display_terminal)(uint32_t x, uint32_t y);
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

uint32_t get_pixel_idx(int x, int y) {
  return x + y * runtime_services.framebuffer_data.width;
}


uint32_t blend_black(uint32_t color1) {
    uint32_t alpha1 = BLEND_GET_BLUE(color1);
    uint32_t red1 = BLEND_GET_RED(color1);
    uint32_t green1 = BLEND_GET_GREEN(color1);
    uint32_t blue1 = BLEND_GET_BLUE(color1);

    uint32_t alpha2 = BLEND_GET_ALPHA(0x000000);
    uint32_t red2 = BLEND_GET_RED(0x000000);
    uint32_t green2 = BLEND_GET_GREEN(0x00000);
    uint32_t blue2 = BLEND_GET_BLUE(0x00000);

    const float BLEND_AMT = 0.5;

    uint32_t r = (uint32_t)((alpha1 * BLEND_AMT / 255) * red1);
    uint32_t g = (uint32_t)((alpha1 * BLEND_AMT / 255) * green1);
    uint32_t b = (uint32_t)((alpha1 * BLEND_AMT / 255) * blue1);

    r += (((255 - alpha1) * BLEND_AMT / 255) * (alpha2 * BLEND_AMT / 255)) * red2;
    g += (((255 - alpha1) * BLEND_AMT / 255) * (alpha2 * BLEND_AMT / 255)) * green2;
    b += (((255 - alpha1) * BLEND_AMT / 255) * (alpha2 * BLEND_AMT / 255)) * blue2;

    uint32_t new_alpha = (uint32_t)(alpha1 + ((255 - alpha1) * BLEND_AMT / 255) * alpha2);
    uint32_t blend_res = (new_alpha << 24) |  (r << 16) | (g << 8) | (b << 0);
    return blend_res;
}


// Just puts it on one section of screen.
void blit_wallpaper(uint32_t xpos, uint32_t ypos) {
    char* img = runtime_services.wallpaper->pixel_data;
    uint32_t* screen = runtime_services.framebuffer_data.base_addr;

    struct BMP* bmp = runtime_services.wallpaper; 
    uint32_t last_pixel = 0;

    unsigned int j = 0;

    for (uint64_t y = 0; y < runtime_services.framebuffer_data.height; ++y) {
        char* image_row = img + y * bmp->info_header.width * 3;
        j = 0;
        for (uint64_t x = 0; x < runtime_services.framebuffer_data.width; ++x) {
            if (x < runtime_services.wallpaper->info_header.width && y < runtime_services.wallpaper->info_header.height) {
                uint32_t b = image_row[j++];
                uint32_t g = image_row[j++];
                uint32_t r = image_row[j++];

                // Keep track of last pixel so we can copy it after x > wallpaper_width.
                last_pixel = (((r << 16) | (g << 8) | (b)) & 0x00FFFFFF) | 0xFF000000;
                screen[get_pixel_idx(((xpos + bmp->info_header.width) / 2) + x, ypos + bmp->info_header.height - 1 - y)] = last_pixel;
            } else {
                // X > wallpaper_width so we must now copy the last_x_pixel.
                screen[get_pixel_idx(((xpos + bmp->info_header.width) / 2) + x, ypos + bmp->info_header.height - 1 - y)] = last_pixel;
            } 
        }
    }
}

void display_terminal(uint32_t xpos, uint32_t ypos) {
    const uint64_t WIDTH = 1000;
    const uint64_t HEIGHT = 500;

    uint32_t* screen = runtime_services.framebuffer_data.base_addr;

    for (uint64_t y = ypos; y < HEIGHT; ++y) {
        for (uint64_t x = xpos; x < WIDTH; ++x) {
            uint32_t old_pixel = screen[get_pixel_idx(x, y)];
            screen[get_pixel_idx(x, y)] = blend_black(old_pixel);
        }
    }

#if DRAW_OUTLINE
    // Draw a cool outline on the window.
    // Draw down on left side of window.
    const uint32_t EDGE_DISTANCE = 4;                   // Distance of outline to outer edges of window.
    const uint32_t OUTLINE_COLOR = 0x808080;
    for (uint64_t y = ypos + EDGE_DISTANCE; y < HEIGHT - EDGE_DISTANCE; ++y) {
        screen[get_pixel_idx(xpos + EDGE_DISTANCE, y)] = OUTLINE_COLOR;
    }

    // Draw right on bottom of window.
    for (uint64_t x = xpos + EDGE_DISTANCE; x < WIDTH - EDGE_DISTANCE; ++x) {
        screen[get_pixel_idx(x, HEIGHT - EDGE_DISTANCE)] = OUTLINE_COLOR;
    }

    // Draw up on right of window.
    for (uint64_t y = HEIGHT - EDGE_DISTANCE; y > ypos + EDGE_DISTANCE; --y) {
        screen[get_pixel_idx(WIDTH - EDGE_DISTANCE, y)] = OUTLINE_COLOR;
    }

     // Draw right on top of window.
     // Now we will have an outline!
    for (uint64_t x = xpos + EDGE_DISTANCE; x < WIDTH - EDGE_DISTANCE; ++x) {
        screen[get_pixel_idx(x, ypos + EDGE_DISTANCE)] = OUTLINE_COLOR;
    }

#endif
}


// Displays it on whole screen.
void display_wallpaper(void) {
        blit_wallpaper(50, 0);
        blit_wallpaper(50, runtime_services.wallpaper->info_header.height);
        blit_wallpaper(50, runtime_services.wallpaper->info_header.height*2);
        blit_wallpaper(runtime_services.framebuffer_data.width + (runtime_services.wallpaper->info_header.width), runtime_services.wallpaper->info_header.height/3);
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
    runtime_services.wallpaper = wallpaper;

    if (!(wallpaper)) {
        Print(L"Could not load wallpaper!\n");
    } else {
        // Verify that the signature is equal to 'BM'.
        if ((wallpaper->header.signature & 0xFF) == 'B' && (wallpaper->header.signature >> 8) == 'M') {
            read_wallpaper_data(wallpaper);               // Dump data if verbose mode is on.
            display_wallpaper();
            // Setup runtime services.
            runtime_services.display_wallpaper = display_wallpaper;
            runtime_services.display_terminal = display_terminal;

            // Display some things.
            display_terminal(250, 50);                                   // Display boot menu.
        }
    }
#endif

    // Setup the memory map.
    EFI_MEMORY_DESCRIPTOR* map = NULL;
    UINTN mapSize, mapKey, descSize;
    UINT32 descVersion;

    sysTable->BootServices->GetMemoryMap(&mapSize, map, &mapKey, &descSize, &descVersion);
    sysTable->BootServices->AllocatePool(EfiLoaderData, mapSize, (void**)&map);
    sysTable->BootServices->GetMemoryMap(&mapSize, map, &mapKey, &descSize, &descVersion);          // Load memory map into memory.
    
    runtime_services.mmap.map = map;
    runtime_services.mmap.mapSize = mapSize;
    runtime_services.mmap.mapDescSize = descSize;

    __asm__ __volatile__("cli; hlt");

    return EFI_SUCCESS;
}
