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

// Macros for PSF1 font.
#define PSF1_MAGIC0 0x00000036
#define PSF1_MAGIC1 0x00000004
#define PSF1_HEADER_SIZE 4
#define TITLE "FacelessBoot v0.0.1"

// One if we are in boot menu.
uint8_t boot_mode = 1;

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

    struct PSF1_FONT_HEADER {
        unsigned char magic[2];
        unsigned char mode;
        unsigned char chsize;
    } *psf1_font_header;

    struct PSF1_FONT { 
        void* glyphBuffer;
    } *psf1_font;

    struct Canvas {
        uint32_t x;
        uint32_t y;
    } canvas;

    struct Terminal {
        uint32_t x;
        uint32_t y;
        uint32_t w;         // Width.
        uint32_t h;         // Height.
        uint32_t c_x;       // Cursor x.
        uint32_t c_y;       // Cursor y.
    } terminal;
    
    struct BMP* wallpaper;

    // SERVICE WILL BE NULL IF IT IS NOT AVAILABLE.
    // MAKE SURE TO CHECK BEFORE USING IT.
    void(*display_wallpaper)(void);
    void(*display_terminal)(uint32_t x, uint32_t y);
    void(*framebuffer_write)(const char* str, uint32_t color, uint32_t restore_to);
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


size_t strlen(const char* str) {
    size_t n = 0;
    while (str[n++]);
    return n - 1;
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


void load_font(EFI_FILE* dir, CHAR16* path, EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* sysTable) {
    EFI_FILE* font = load_file(dir, path, imageHandle, sysTable);

    // Font does not exist!
    if (!(font)) {
        runtime_services.psf1_font_header = NULL;
        return;
    }

    // Allocate memory for our font.
    sysTable->BootServices->AllocatePool(EfiLoaderData, PSF1_HEADER_SIZE, (void**)&runtime_services.psf1_font_header);
    UINTN header_size = PSF1_HEADER_SIZE;
    font->Read(font, &header_size, runtime_services.psf1_font_header);

    // Magic bytes incorrect.
    if (!(runtime_services.psf1_font_header->magic[0] & PSF1_MAGIC0) || !(runtime_services.psf1_font_header->magic[1] & PSF1_MAGIC1)) {
        runtime_services.psf1_font_header = NULL;
        return;;
    }

    UINTN glyphBufferSize = runtime_services.psf1_font_header->chsize * 256;
    if (runtime_services.psf1_font_header->mode == 1) {
       glyphBufferSize = runtime_services.psf1_font_header->chsize * 512; 
    }

    void* glyphBuffer = NULL;
    font->SetPosition(font, PSF1_HEADER_SIZE);
    sysTable->BootServices->AllocatePool(EfiLoaderData, glyphBufferSize, (void**)&glyphBuffer);

    // Read glpyhs into memory.
    font->Read(font, &glyphBufferSize, glyphBuffer);
    
    // Allocate memory for font.
    sysTable->BootServices->AllocatePool(EfiLoaderData, glyphBufferSize+PSF1_HEADER_SIZE, (void**)&runtime_services.psf1_font);

    // Set font glpyh buffer.
    runtime_services.psf1_font->glyphBuffer = glyphBuffer;
}

uint32_t get_pixel_idx(int x, int y) {
  return x + y * runtime_services.framebuffer_data.width;
}

void putChar(unsigned int color, char chr, unsigned int xOff, unsigned int yOff) {
    unsigned int* pixPtr = (unsigned int*)runtime_services.framebuffer_data.base_addr;
    char* fontPtr = runtime_services.psf1_font->glyphBuffer + (chr * runtime_services.psf1_font_header->chsize);
    for (unsigned long y = yOff; y < yOff + 16; y++){
        for (unsigned long x = xOff; x < xOff+8; x++){
            if ((*fontPtr & (0b10000000 >> (x - xOff))) > 0){
                    *(unsigned int*)(pixPtr + x + (y * runtime_services.framebuffer_data.ppsl)) = color;
                }

        }
        fontPtr++;
    }
}


/*
 *  @str: String to print.
 *  @color: Color you want the string to be.
 *  @restore_to: When the x position needs to be reset (i.e during newline)
 *  it will reset to restore_to.
 *
 */

void lfb_write(const char* str, uint32_t color, uint32_t restore_to) {
    size_t str_sz = strlen(str);
    
    for (size_t i = 0; i < str_sz; ++i) {
        if (str[i] == '\n') {
            runtime_services.canvas.x = restore_to;
            runtime_services.canvas.y += 20;
            continue;
        }

        putChar(color, str[i], runtime_services.canvas.x+8, runtime_services.canvas.y);
        runtime_services.canvas.x += 8;             // Increment canvas x.
    }
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


void term_write(const char* str, uint32_t color) {
    // Save old canvas position.
    uint32_t old_canvas_x = runtime_services.canvas.x, old_canvas_y = runtime_services.canvas.y;
    // Set canvas position to where we want to write on terminal.
    runtime_services.canvas.x = ((runtime_services.terminal.x + 20)) + runtime_services.terminal.c_x;
    runtime_services.canvas.y = runtime_services.terminal.y + 20 + runtime_services.terminal.c_y + 10;
    lfb_write(str, color, ((runtime_services.terminal.x + 20)) + runtime_services.terminal.c_x);

    // Get string size.
    size_t str_sz = strlen(str);

    // Increment cursor x by string size.
    runtime_services.terminal.c_x += (str_sz * 8);
    
    // Restore old canvas position.
    runtime_services.canvas.x = old_canvas_x;
    runtime_services.canvas.y = old_canvas_y;
}

void term_write_xy(const char* str, uint32_t color, uint32_t x, uint32_t y) {
    // Get string size.
    size_t str_sz = strlen(str);

    // Write it out.
    for (size_t i = 0; i < str_sz; ++i) {
        putChar(color, str[i], x, y);
        x += 8;
    }
}


void display_terminal(uint32_t xpos, uint32_t ypos) {
    const uint64_t WIDTH = 1000;
    const uint64_t HEIGHT = 500;

    runtime_services.terminal.x = xpos;
    runtime_services.terminal.y = ypos;
    runtime_services.terminal.w = WIDTH;
    runtime_services.terminal.h = HEIGHT;
    runtime_services.terminal.c_x = 0;
    runtime_services.terminal.c_y = 0;

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
    uint32_t edge_distance = 4;                   // Distance of outline to outer edges of window.

    if (boot_mode) {
        edge_distance = 20;
    }
        
    const uint32_t OUTLINE_COLOR = 0x808080;
    for (uint64_t y = ypos + edge_distance; y < HEIGHT - edge_distance; ++y) {
        screen[get_pixel_idx(xpos + edge_distance, y)] = OUTLINE_COLOR;
    }

    // Draw right on bottom of window.
    for (uint64_t x = xpos + edge_distance; x < WIDTH - edge_distance; ++x) {
        screen[get_pixel_idx(x, HEIGHT - edge_distance)] = OUTLINE_COLOR;
    }

    // Draw up on right of window.
    for (uint64_t y = HEIGHT - edge_distance; y > ypos + edge_distance; --y) {
        screen[get_pixel_idx(WIDTH - edge_distance, y)] = OUTLINE_COLOR;
    }

     // Draw right on top of window.
     // Now we will have an outline!
    for (uint64_t x = xpos + edge_distance; x < WIDTH - edge_distance; ++x) {
        screen[get_pixel_idx(x, ypos + edge_distance)] = OUTLINE_COLOR;
    }

#endif

    // Draw title bar.
    if (boot_mode) {
        size_t title_length = strlen(TITLE);
        uint32_t xpos_mid = (xpos*2)+(WIDTH/title_length);
        term_write_xy(TITLE, 0xA9A9A9, xpos_mid, ypos);
    }
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
    
    runtime_services.canvas.x = 0;
    runtime_services.canvas.y = 0;

    // Load font.
    load_font(NULL, PSF1_FONT_PATH, imageHandle, sysTable);

    if (runtime_services.psf1_font_header == NULL) {
        Print(L"Could not load %s.\n", PSF1_FONT_PATH);
        __asm__ __volatile__("cli; hlt");   
    }

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
#else
    runtime_services.display_wallpaper = NULL;
    runtime_services.display_terminal = NULL;
#endif 


    runtime_services.framebuffer_write = lfb_write;
    term_write("Hello, World!\nHello, World!", 0xFF0000);

    __asm__ __volatile__("cli; hlt");

    return EFI_SUCCESS;
}
