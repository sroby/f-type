#include <string.h>

#include "machine.h"
#include "window.h"
#include "cpu.h"
#include "ppu.h"
#include "memory_maps.h"

void machine_loop(const Cartridge *cart, const DebugMap *dbg_map, Window *wnd) {
    // Create and initialize the hardware
    CPUState cpu;
    PPUState ppu;
    MemoryMap cpu_mm, ppu_mm;
    MemoryMapCPUInternal cpu_mm_i;
    MemoryMapPPUInternal ppu_mm_i;
    
    cpu_init(&cpu, &cpu_mm);
    ppu_init(&ppu, &ppu_mm, &cpu);
    memory_map_cpu_init(&cpu_mm, &cpu_mm_i, cart, &ppu);
    memory_map_ppu_init(&ppu_mm, &ppu_mm_i, cart);

    const char *const verb_char = getenv("VERBOSE");
    const bool verbose = verb_char ? *verb_char - '0' : false;

    cpu_reset(&cpu);

    // Main loop
    bool quitting = false;
    do {
        // Advance one frame
        bool done = false;
        do {
            // Check for debug label
            bool is_endless_loop = false;
            if (verbose && dbg_map) {
                int i = 0;
                while (dbg_map[i].label[0]) {
                    if (dbg_map[i].addr == cpu.pc) {
                        const char *label = dbg_map[i].label;
                        if (strcmp(label, "EndlessLoop")) {
                            printf(":%s\n", dbg_map[i].label);
                        } else {
                            is_endless_loop = true;
                        }
                        break;
                    }
                    i++;
                }
            }
            // Run next CPU instruction
            if (cpu_step(&cpu, verbose && !is_endless_loop) < 0x100) {
                break;
            }
            // Check for PPU scanline, and possibly end of frame
            while (ppu.t * T_SCANLINE_PER_CPU < cpu.t * T_MULTI) {
                if (verbose) {
                    printf("--scanline %d--\n", ppu.scanline);
                }
                if (ppu_scanline(&ppu)) {
                    done = true;
                }
            }
        } while (!done);
        
        // Render the frame
        window_update_screen(wnd, &ppu);
        
        // Process events
        if (window_process_events(wnd, cpu_mm_i.controllers)) {
            quitting = true;
        }
        
        // TODO: Delay next frame
        // Currently relying on vsync to slow down things for now
    } while (!quitting);
    
    printf("Ended after %llu frames\n", ppu.t / 262);
}
