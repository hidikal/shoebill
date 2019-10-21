/*
 * Copyright (c) 2013, Peter Rutenbar <pruten@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <strings.h>
#include <assert.h>
#include <pthread.h>

#include <GLUT/glut.h>

#include "debugger.h"

rb_tree *keymap;

static uint8_t lookup_special(int special)
{
    switch (special) {
        case GLUT_KEY_UP: return 0x3e;
        case GLUT_KEY_DOWN: return 0x3d;
        case GLUT_KEY_LEFT: return 0x3b;
        case GLUT_KEY_RIGHT: return 0x3c;
        default: return 0xff;
    }
}

static uint32_t _get_modifiers (void)
{
    int glut_modifiers = glutGetModifiers();
    uint32_t result = 0;
    
    result |= (glut_modifiers & GLUT_ACTIVE_SHIFT) ? (1 << 17) : 0;
    result |= (glut_modifiers & GLUT_ACTIVE_CTRL) ? (1 << 18) : 0;
    result |= (glut_modifiers & GLUT_ACTIVE_ALT) ? (1 << 19) : 0;
    
    return result;
}

void global_mouse_func (int button, int state, int x, int y)
{
    shoebill_mouse_click(state == GLUT_DOWN);
    shoebill_mouse_move(x, y);
}

void global_motion_func (int x, int y)
{
    shoebill_mouse_click(1);
    shoebill_mouse_move(x, y);
}

void global_passive_motion_func (int x, int y)
{
    shoebill_mouse_click(0);
    shoebill_mouse_move(x, y);
}

void global_keyboard_up_func (unsigned char c, int x, int y)
{
    uint16_t value;
    if (rb_find(keymap, c, &value)) {
        shoebill_key_modifier((value >> 8) | (_get_modifiers() >> 16));
        shoebill_key(0, value & 0xff);
    }
}

void global_keyboard_down_func (unsigned char c, int x, int y)
{
    uint16_t value;
    if (rb_find(keymap, c, &value)) {
        shoebill_key_modifier((value >> 8) | (_get_modifiers() >> 16));
        shoebill_key(1, value & 0xff);
    }
}

void global_special_up_func (int special, int x, int y)
{
    const uint8_t code = lookup_special(special);
    if (code != 0xff) {
        shoebill_key_modifier(_get_modifiers() >> 16);
        shoebill_key(0, code);
    }
}

void global_special_down_func (int special, int x, int y)
{
    const uint8_t code = lookup_special(special);
    if (code != 0xff) {
        shoebill_key_modifier(_get_modifiers() >> 16);
        shoebill_key(1, code);
    }
}

void timer_func (int arg)
{
    glutTimerFunc(15, timer_func, 0); // 15ms = 66.67hz
    glutPostRedisplay();
}


void _display_func (void)
{
    shoebill_video_frame_info_t frame = shoebill_get_video_frame(9, 0);
    
    shoebill_send_vbl_interrupt(9);
    
    glDrawBuffer(GL_BACK);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glClearColor(0.0, 0.0, 0.0, 0.0);
    
    glViewport(0, 0, frame.width, frame.height);
    glRasterPos2i(0, frame.height);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    glPixelZoom(1.0, -1.0);
    
    glDrawPixels(frame.width,
                 frame.height,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 frame.buf);
    
    glutSwapBuffers();
}

static void _init_keyboard_map (void)
{
    #define mapkeymod(u, a, m) do { \
        assert((a >> 7) == 0); \
        uint16_t value = ((m) << 8)| (a); \
        rb_insert(keymap, u, &value, NULL); \
    } while (0)
    
    #define mapkey(_u, a) mapkeymod(_u, a, 0)

    keymap = rb_new(p_new_pool(NULL), sizeof(uint16_t));
    
    // Letters
    mapkey('a', 0x00);
    mapkey('b', 0x0b);
    mapkey('c', 0x08);
    mapkey('d', 0x02);
    mapkey('e', 0x0e);
    mapkey('f', 0x03);
    mapkey('g', 0x05);
    mapkey('h', 0x04);
    mapkey('i', 0x22);
    mapkey('j', 0x26);
    mapkey('k', 0x28);
    mapkey('l', 0x25);
    mapkey('m', 0x2e);
    mapkey('n', 0x2d);
    mapkey('o', 0x1f);
    mapkey('p', 0x23);
    mapkey('q', 0x0c);
    mapkey('r', 0x0f);
    mapkey('s', 0x01);
    mapkey('t', 0x11);
    mapkey('u', 0x20);
    mapkey('v', 0x09);
    mapkey('w', 0x0d);
    mapkey('x', 0x07);
    mapkey('y', 0x10);
    mapkey('z', 0x06);
    
    // Numbers
    mapkey('0', 0x1d);
    mapkey('1', 0x12);
    mapkey('2', 0x13);
    mapkey('3', 0x14);
    mapkey('4', 0x15);
    mapkey('5', 0x17);
    mapkey('6', 0x16);
    mapkey('7', 0x1a);
    mapkey('8', 0x1c);
    mapkey('9', 0x19);
    
    // Top row symbols
    mapkeymod(')', 0x1d, modShift);
    mapkeymod('!', 0x12, modShift);
    mapkeymod('@', 0x13, modShift);
    mapkeymod('#', 0x14, modShift);
    mapkeymod('$', 0x15, modShift);
    mapkeymod('%', 0x17, modShift);
    mapkeymod('^', 0x16, modShift);
    mapkeymod('&', 0x1a, modShift);
    mapkeymod('*', 0x1c, modShift);
    mapkeymod('(', 0x19, modShift);
    
    // Other symbols (no shift)
    mapkeymod('`', 0x32, 0);
    mapkeymod('-', 0x1b, 0);
    mapkeymod('=', 0x18, 0);
    mapkeymod('[', 0x21, 0);
    mapkeymod(']', 0x1e, 0);
    mapkeymod('\\', 0x2a, 0);
    mapkeymod(';', 0x29, 0);
    mapkeymod('\'', 0x27, 0);
    mapkeymod(',', 0x2b, 0);
    mapkeymod('.', 0x2f, 0);
    mapkeymod('/', 0x2c, 0);
    
    // Other symbols (with shift)
    mapkeymod('~', 0x32, modShift);
    mapkeymod('_', 0x1b, modShift);
    mapkeymod('+', 0x18, modShift);
    mapkeymod('{', 0x21, modShift);
    mapkeymod('}', 0x1e, modShift);
    mapkeymod('|', 0x2a, modShift);
    mapkeymod(':', 0x29, modShift);
    mapkeymod('"', 0x27, modShift);
    mapkeymod('<', 0x2b, modShift);
    mapkeymod('>', 0x2f, modShift);
    mapkeymod('?', 0x2c, modShift);
    
    // Function keys
    /*mapkey(NSF1FunctionKey, 0x7a);
    mapkey(NSF2FunctionKey, 0x78);
    mapkey(NSF3FunctionKey, 0x63);
    mapkey(NSF4FunctionKey, 0x76);
    mapkey(NSF5FunctionKey, 0x60);
    mapkey(NSF6FunctionKey, 0x61);
    mapkey(NSF7FunctionKey, 0x62);
    mapkey(NSF8FunctionKey, 0x64);
    mapkey(NSF9FunctionKey, 0x65);
    mapkey(NSF10FunctionKey, 0x6d);
    mapkey(NSF11FunctionKey, 0x67);
    mapkey(NSF12FunctionKey, 0x6f);
    mapkey(NSF13FunctionKey, 0x69);
    mapkey(NSF14FunctionKey, 0x6b);
    mapkey(NSF15FunctionKey, 0x71);*/
    
    // Arrows
    /*mapkey(NSUpArrowFunctionKey, 0x3e);
    mapkey(NSDownArrowFunctionKey, 0x3d);
    mapkey(NSRightArrowFunctionKey, 0x3c);
    mapkey(NSLeftArrowFunctionKey, 0x3b);*/
    
    // Delete
    //mapkey(NSDeleteFunctionKey, 0x75);
    mapkey(0x08, 0x33);
    mapkey(0x7f, 0x33);
    
    // Enter, NL, CR
    mapkey('\r', 0x24);
    mapkey('\n', 0x24);
    mapkey(0x03, 0x24);
    
    // Other keys
    mapkey(0x1b, 0x35); // escape
    mapkey(' ', 0x31); // space
    mapkey('\t', 0x30); // tab
}

static void _init_glut_video (void)
{
    shoebill_video_frame_info_t frame = shoebill_get_video_frame(9, 1);
    
    glutInitWindowSize(frame.width, frame.height);
    glutCreateWindow("Shoebill");
    glutDisplayFunc(_display_func);
    glutIgnoreKeyRepeat(1);
    
    
    glutKeyboardFunc(global_keyboard_down_func);
    glutKeyboardUpFunc(global_keyboard_up_func);
    
    glutSpecialFunc(global_special_down_func);
    glutSpecialUpFunc(global_special_up_func);
    
    glutMouseFunc(global_mouse_func);
    glutMotionFunc(global_motion_func);
    glutPassiveMotionFunc(global_passive_motion_func);
    
    glutInitDisplayMode (GLUT_DOUBLE);
    
    glShadeModel(GL_FLAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glClearColor(0.1, 1.0, 0.1, 1.0);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, frame.width, 0, frame.height, -1.0, 1.0);
    
    glViewport(0, 0,  frame.width, frame.height);
    
}

int main (int argc, char **argv)
{
    shoebill_config_t config;
    pthread_t pid;
    
    
    bzero(&config, sizeof(shoebill_config_t));
    
    /*
     * A variety of hacky things happen in debug mode.
     * shoebill_start() will not create a new thread to run
     * the CPU loop. We'll create a CPU thread here, bypass
     * core_api, and directly manipulate the emulator guts.
     *
     * This is not a great example of how to write a GUI 
     * for shoebill...
     */
    config.debug_mode = 1;
     
    config.aux_verbose = 0;
    config.ram_size = 16 * 1024 * 1024;
    config.aux_kernel_path = "/unix";
    config.rom_path = "../../../shoebill_priv/macii.rom";
    

    config.scsi_devices[0].path = "../../../shoebill_priv/root3.img";
    //config.scsi_devices[1].path = "../priv/marathon.img";
    
    /*dbg_state.ring_len = 256 * 1024 * 1024;
    dbg_state.ring = malloc(dbg_state.ring_len);
    dbg_state.ring_i = 0;*/
    
    shoebill_validate_or_zap_pram(config.pram, 1);
    
    if (!shoebill_initialize(&config)) {
        printf("%s\n", config.error_msg);
        return 0;
    }
    
    _init_keyboard_map();
    
    shoebill_install_video_card(&config,
                                9, // slotnum
                                640, // 1024,
                                480); // 768,
    
    // uint8_t ethernet_addr[6] = {0x22, 0x33, 0x55, 0x77, 0xbb, 0xdd};
    // shoebill_install_ethernet_card(&config, 13, ethernet_addr);
    
    // Start the VIA timer thread
    shoebill_start();
    
    // Create a new thread to drive the CPU & debugger UI
    pthread_create(&pid, NULL, cpu_debugger_thread, NULL);
    
    int dummyargc = 1;
    glutInit(&dummyargc, argv);
    
    // Create/configure the screen
    _init_glut_video();
    
    // Set a GLUT timer to update the screen
    glutTimerFunc(15, timer_func, 0);
    
    glutMainLoop();
    
    return 0;
}


