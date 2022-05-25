#ifndef FACELESS_BOOT_PROTOCOL_H
#define FACELESS_BOOT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    MMAP_RESERVED,
    MMAP_EFI_LOADER_CODE,
    MMAP_EFI_LOADER_DATA,
    MMAP_EFI_BOOTSERVICES_CODE,
    MMAP_EFI_BOOTSERVICES_DATA,
    MMAP_EFI_RUNTIME_SERVICES_CODE,
    MMAP_EFI_RUNTIME_SERVICES_DATA,
    MMAP_USABLE_MEMORY,
    MMAP_UNUSABLE_MEMORY,
    MMAP_ACPI_RECLAIM_MEMORY,
    MMAP_ACPI_MEMORY_NVS,
    MMAP_MEMORY_MAPPED_IO,
    MMAP_MEMORY_MAPPED_IO_PORT_SPACE,
    MMAP_EFI_PAL_CODE,
} mem_type_t;


struct FacelessMemoryDescriptor {
    uint32_t type;
    void* physAddr;
    void* virtAddr;
    uint64_t nPages;
    uint64_t attr;
};


struct FacelessMemoryInfo {
    struct FacelessMemoryDescriptor* mMap;
    uint64_t mSize;
    uint64_t mDescriptorSize;
};


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
        struct FacelessMemoryDescriptor* map;
        uint64_t mapSize;
        uint64_t mapDescSize;
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
    void(*refresh_wallpaper)(void);
    void(*display_terminal)(uint32_t x, uint32_t y);
    void(*framebuffer_write)(const char* str, uint32_t color, uint32_t restore_to);
    void(*term_write)(const char* str, uint32_t color);
} runtime_services;

#endif
