
#include <stdlib.h>
#include <string.h>

#include <stdio.h>


// include NESLIB header
#include "neslib.h"

// include CC65 NES Header (PPU)
#include <nes.h>

// link the pattern table into CHR ROM
//#link "chr_generic.s"

///// METASPRITES

// define a 2x2 metasprite
#define DEF_METASPRITE_2x2(name,code,pal)\
const unsigned char name[]={\
        0,      0,      (code)+0,   pal, \
        0,      8,      (code)+1,   pal, \
        8,      0,      (code)+2,   pal, \
        8,      8,      (code)+3,   pal, \
        128};

// define a 2x2 metasprite, flipped horizontally
#define DEF_METASPRITE_2x2_FLIP(name,code,pal)\
const unsigned char name[]={\
        8,      0,      (code)+0,   (pal)|OAM_FLIP_H, \
        8,      8,      (code)+1,   (pal)|OAM_FLIP_H, \
        0,      0,      (code)+2,   (pal)|OAM_FLIP_H, \
        0,      8,      (code)+3,   (pal)|OAM_FLIP_H, \
        128};

// BCD arithmetic support
#include "bcd.h"
//#link "bcd.c"

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

// number of rows in scrolling playfield (without status bar)
#define PLAYROWS 30
#define PLAYCOLUMNS 32

/// GLOBAL VARIABLES

word x_scroll;		// X scroll amount in pixels
byte seg_height;	// segment height in metatiles
byte seg_width;		// segment width in metatiles
byte seg_char;		// character to draw
byte seg_palette;	// attribute table value

// buffers that hold vertical slices of nametable data
char ntbuf1[PLAYROWS];	// left side
char ntbuf2[PLAYROWS];	// right side

// a vertical slice of attribute table entries
char attrbuf[PLAYROWS/4];

// nos sprites de puyo
DEF_METASPRITE_2x2(puyo1, 0xd8, 0);
DEF_METASPRITE_2x2(puyo2, 0xd8, 1);
DEF_METASPRITE_2x2(puyo3, 0xdc, 0);
DEF_METASPRITE_2x2(puyo4, 0xdc, 1);

// number of actors (4 h/w sprites each)
// 12 : 2 active on each players screen
// and 4 for preview on each side
#define NUM_ACTORS 12

// actor x/y positions
byte actor_x[NUM_ACTORS];
byte actor_y[NUM_ACTORS];
// actor x/y deltas per frame (signed)
sbyte actor_dx[NUM_ACTORS];
sbyte actor_dy[NUM_ACTORS];

//INPUT BUFFER DELAY
#define INPUT_DIRECTION_DELAY 4
#define INPUT_BUTTON_DELAY 4

const unsigned char* const puyoSeq[4] = {
  puyo1, puyo2, puyo3, puyo4,
};

// convert from nametable address to attribute table address
word nt2attraddr(word a) {
  return (a & 0x2c00) | 0x3c0 |
    ((a >> 4) & 0x38) | ((a >> 2) & 0x07);
}

// draw metatile into nametable buffers
// y is the metatile coordinate (row * 2)
// ch is the starting tile index in the pattern table
void set_metatile(byte y, byte ch) {
  ntbuf1[y*2] = ch;
  ntbuf1[y*2+1] = ch+1;
  ntbuf2[y*2] = ch+2;
  ntbuf2[y*2+1] = ch+3;
}

// set attribute table entry in attrbuf
// x and y are metatile coordinates
// pal is the index to set
void set_attr_entry(byte x, byte y, byte pal) {
  if (y&1) pal <<= 4;
  if (x&1) pal <<= 2;
  attrbuf[y/2] |= pal;
}

// write attribute table buffer to vram buffer
void put_attr_entries(word addr) {
  byte i;
  for (i=0; i<PLAYROWS/4; i++) {
    VRAMBUF_PUT(addr, attrbuf[i], 0);
    addr += 8;
  }
  vrambuf_end();
}

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x0D,			// screen color

  0x11,0x30,0x27,0x00,	// background palette 0
  0x1C,0x20,0x2C,0x00,	// background palette 1
  0x00,0x10,0x20,0x00,	// background palette 2
  0x06,0x16,0x26,0x00,   // background palette 3

  0x16,0x35,0x24,0x00,	// sprite palette 0
  0x00,0x37,0x25,0x00,	// sprite palette 1
  0x0D,0x2D,0x3A,0x00,	// sprite palette 2
  0x0D,0x27,0x2A	// sprite palette 3
};

void build_field()
{
  //register word addr;
  //byte i, x, y;
  byte x, y;
  /*vram_adr(NTADR_A(10,10));
  vram_put(0xc4);
  vram_put(0xc6);
  vram_adr(NTADR_A(10,11));
  vram_put(0xc5);
  vram_put(0xc7);*/
  for (x = 0; x < PLAYCOLUMNS; x+=2)
  {
    if (x == 0 || x == 30)
    {
      for (y = 0; y < PLAYROWS; y+=2)
      {
        vram_adr(NTADR_A(x,y));
        vram_put(0xc4);
        vram_put(0xc6);
        vram_adr(NTADR_A(x,y+1));
        vram_put(0xc5);
        vram_put(0xc7);
      }
    }
    else if (x == 14 || x == 15)
    {/* il faudra ici mettre les puyo à venir !*/
      for (y = 0; y < PLAYROWS; y+=2)
      {
        vram_adr(NTADR_A(x,y));
        vram_put(0xc4);
        vram_put(0xc6);
        vram_adr(NTADR_A(x,y+1));
        vram_put(0xc5);
        vram_put(0xc7);
      }
    }
    else if (x == 16 || x == 17)
    {
      for (y = 0; y < PLAYROWS; y+=2)
      {
        vram_adr(NTADR_A(x,y));
        vram_put(0xc4);
        vram_put(0xc6);
        vram_adr(NTADR_A(x,y+1));
        vram_put(0xc5);
        vram_put(0xc7);
      }
    }
    else
    {//le haut/ bas de l'air de jeu
      vram_adr(NTADR_A(x,0));
      vram_put(0xc4);
      vram_put(0xc6);
      vram_adr(NTADR_A(x,1));
      vram_put(0xc5);
      vram_put(0xc7);
      vram_adr(NTADR_A(x,PLAYROWS-4));
      vram_put(0xc4);
      vram_put(0xc6);
      vram_adr(NTADR_A(x,PLAYROWS-3));
      vram_put(0xc5);
      vram_put(0xc7);
      vram_adr(NTADR_A(x,PLAYROWS-2));
      vram_put(0xc4);
      vram_put(0xc6);
      vram_adr(NTADR_A(x,PLAYROWS-1));
      vram_put(0xc5);
      vram_put(0xc7);
    }
    
  }
  //colonne 0 et 31 sont pleines 
  //On peut pas mettre plus de 3 colonnes dans le buffer...
  /*
  vrambuf_clear();
  memset(ntbuf1, 0, sizeof(ntbuf1));
  memset(ntbuf2, 0, sizeof(ntbuf2));
  for (i = 0; i < PLAYROWS/2 - 1; i++)
  {
    set_metatile(i,0xc4);
  }
  addr = NTADR_A(0, 0);
  vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, PLAYROWS);
  vrambuf_put((addr+1)|VRAMBUF_VERT, ntbuf2, PLAYROWS);
  //vrambuf_flush();
  addr = NTADR_A(2, 0);
  vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, PLAYROWS);
  //vrambuf_put((addr+1)|VRAMBUF_VERT, ntbuf2, PLAYROWS);
  */
}

// setup PPU and tables
void setup_graphics() {
  // clear sprites
  oam_clear();
   // clear sprites
  oam_hide_rest(0);
  // set palette colors
  pal_all(PALETTE);
   // clear vram buffer
  vrambuf_clear();
  set_vram_update(updbuf);
  seg_palette = rand8() & 3;
  seg_char = 0xe0;
}

void main(void)
{
  char oam_id;	// sprite ID
  char i;	// actor index
  char pad;	// controller flags
  char str[32];
  char previous_pad[2];
  char input_delay_PAD_LEFT[2]; //to avoid multiple input on one press
  char input_delay_PAD_RIGHT[2]; //to avoid multiple input on one press
  char input_delay_PAD_A[2]; //to avoid multiple input on one press
  char input_delay_PAD_B[2]; //to avoid multiple input on one press
  char column_height[12]; // heigth of the stack, 0 to 5 p1, 6 to 11 P2, may not be the best strategy
  register word addr;

  setup_graphics();
  // draw message  
  vram_adr(NTADR_A(2,3));
  vram_write("HELLO, WORLD!", 12);
  build_field();
  // initialize actors
  //P1
  actor_x[0] = 3*16;
  actor_y[0] = 0*16;
  actor_dx[0] = 0;
  actor_dy[0] = 1;
  actor_x[1] = 3*16;
  actor_y[1] = 1*16;
  actor_dx[1] = 0;
  actor_dy[1] = 1;
  //P2
  actor_x[2] = 11*16;
  actor_y[2] = 0*16;
  actor_dx[2] = 0;
  actor_dy[2] = 1;
  actor_x[3] = 11*16;
  actor_y[3] = 1*16;
  actor_dx[3] = 0;
  actor_dy[3] = 1;
  previous_pad[0] = 0;
  previous_pad[1] = 0;
  input_delay_PAD_A[0] = 0;
  input_delay_PAD_A[1] = 0;
  input_delay_PAD_B[0] = 0;
  input_delay_PAD_B[1] = 0;
  input_delay_PAD_LEFT[0] = 0;
  input_delay_PAD_LEFT[1] = 0;
  input_delay_PAD_RIGHT[0] = 0;
  input_delay_PAD_RIGHT[1] = 0;
  for (i = 0; i < 12 ; i++)
    column_height[i] = 190;

  
  // enable rendering
  ppu_on_all();
  //scroll(0,240);
  // infinite loop
  while(1) {
    //get input
    for (i = 0; i < 2; i++)
    {
      pad = pad_poll(i);
      //update status of controller memory
      if (previous_pad[i]&PAD_LEFT && pad&PAD_LEFT)
        input_delay_PAD_LEFT[i]++;
      else
        input_delay_PAD_LEFT[i] = 0;
      if (previous_pad[i]&PAD_RIGHT && pad&PAD_RIGHT)
        input_delay_PAD_RIGHT[i]++;
      else
        input_delay_PAD_RIGHT[i] = 0;
      if (previous_pad[i]&PAD_A && pad&PAD_A)
        input_delay_PAD_A[i]++;
      else
        input_delay_PAD_A[i] = 0;
      if (previous_pad[i]&PAD_B && pad&PAD_B)
        input_delay_PAD_B[i]++;
      else
        input_delay_PAD_B[i] = 0;

      //you have to look at the leftmost or rightmost puyo
      //p1 puyo 0 & 1, p2 puyo 2 & 3
      if (actor_x[i*2] < actor_x[(i*2)+1])
      {
        //left/right
        if (pad&PAD_LEFT && actor_x[i*2] > (16+(i*128)))
        {
          //add a bit of delay before going again to left
          if (input_delay_PAD_LEFT[i] == 0 || input_delay_PAD_LEFT[i] > INPUT_DIRECTION_DELAY)
          {
            actor_x[i*2] -= 16;
            actor_x[(i*2)+1] -= 16;
          }
        }
        else if (pad&PAD_RIGHT && actor_x[(i*2)+1] < (96+(i*128)))
        {
          if (input_delay_PAD_RIGHT[i] == 0 || input_delay_PAD_RIGHT[i] > INPUT_DIRECTION_DELAY)
          {
            actor_x[i*2] += 16;
            actor_x[(i*2)+1] += 16;
          }
        }
        else
        { //doing nothing
          /*actor_x[i*2] = 0;
          actor_x[(i*2)+1] = 0;*/
        }
        //buttons, the puyo rotating is always the one at the top
        //so with index at 0 (0 p1, 2 p2)
        if (pad&PAD_B && input_delay_PAD_B[i] == 0)
        { 
          //here as puyo[0] < puyo[1] we are at the left, if we press
          //B the puyo will go under the 2nd puyo
          //the delay has to be at 0, because we don't want it to turn automatically
          //you have to press each time        
          actor_y[i*2] += 16;
          actor_x[i*2] += 16;
        }
        if (pad&PAD_A && input_delay_PAD_A[i] == 0)
        { 
          //here as puyo[0] < puyo[1] we are at the left, if we press
          //A the puyo will go over the 2nd puyo
          actor_y[i*2] -= 16;
          actor_x[i*2] += 16;
        }   
      }
      else
      {
        if (pad&PAD_LEFT && actor_x[(i*2)+1] > (16+i*128))
        {
          if (input_delay_PAD_LEFT[i] == 0 || input_delay_PAD_LEFT[i] > INPUT_DIRECTION_DELAY)
          {
            actor_x[i*2] -= 16;
            actor_x[(i*2)+1] -= 16;
          }
        }
        else if (pad&PAD_RIGHT && actor_x[i*2] < (96+i*128))
        {
          if (input_delay_PAD_RIGHT[i] == 0 || input_delay_PAD_RIGHT[i] > INPUT_DIRECTION_DELAY)
          {
            actor_x[i*2] += 16;
            actor_x[(i*2)+1] += 16;
          }
        }
        else
        {//doing nothing
          /*actor_dx[i*2] = 0;
          actor_dx[(i*2)+1] = 0;*/
        }
        
        if (actor_x[i*2] != actor_x[(i*2)+1])
        {
          //puyo[0] > puyo[1], it's on its right
          if (pad&PAD_B && input_delay_PAD_B[i] == 0)
          { 
            //here as puyo[0] > puyo[1] we are at the right, if we press
            //A the puyo will go over the 2nd puyo
            actor_y[i*2] -= 16;
            actor_x[i*2] -= 16;
          }
          if (pad&PAD_A && input_delay_PAD_A[i] == 0)
          { 
            //here as puyo[0] > puyo[1] we are at the right, if we press
            //A the puyo will go under the 2nd puyo
              actor_y[i*2] += 16;
              actor_x[i*2] -= 16;    
          }   
        }
        else
        {
          //same x for both puyo
          //B we go on the left, A we go on the right
          if (pad&PAD_B && input_delay_PAD_B[i] == 0)
          { 
            //we need to know if puyo[0] is above or below puyo[1]
            // the lowest value is higher on the screen !
            if (actor_y[i*2] < actor_y[(i*2)+1])
            {
              //going from up to left
              actor_x[i*2] -= 16;
              actor_y[i*2] += 16; 
            }
            else
            {  
              //going down to right
              actor_x[i*2] += 16;
              actor_y[i*2] -= 16; 
            }
          }
          if (pad&PAD_A && input_delay_PAD_A[i] == 0)
          { 
            if (actor_y[i*2] < actor_y[(i*2)+1])
            {
              // going from up to right
              actor_x[i*2] += 16;
              actor_y[i*2] += 16; 
            }
            else
            {
              //going from down to left
              actor_x[i*2] -= 16;
              actor_y[i*2] -= 16; 
            }   
          } 
        }
      }  
      previous_pad[i] = pad;
    }
    oam_id = 0;
    for (i = 0 ; i < 4 ; i++)
    {
      oam_id = oam_meta_spr(actor_x[i], actor_y[i], oam_id, puyoSeq[i]);
      //actor_x[i] += actor_dx[i];
     // actor_dx[i] = 0;
      actor_y[i] += actor_dy[i];
      /*if (190 < actor_y[i])
        actor_dy[i] = 0;*/
      //test relative to column_height
      if (column_height[(actor_x[i]>>4) - 1] < actor_y[i])
      {
        actor_dy[i] = 0;
        actor_y[i] = column_height[(actor_x[i]>>4) - 1];
        column_height[(actor_x[i]>>4) - 1] -= 16;
        /*if (column_height[(actor_x[i]>>4) - 1] > 200)
          build_field();*/
      }
      //TODO : problème avec le dy qui descend et le reste, 
      //en fait on ne devrait pas utiliser les dx/dy dans le positionnement des puyos
    }
    
    //put_attr_entries(nt2attraddr(addr));

    if (actor_dy[0] == 0 && actor_dy[1] == 0)
    {
      //vrambuf_clear();
      memset(ntbuf1, 0, sizeof(ntbuf1));
      memset(ntbuf2, 0, sizeof(ntbuf2));
  
      set_metatile(0,0xc4);
      set_metatile(1,0xc4);
      addr = NTADR_A((actor_x[0]>>3), (actor_y[0]>>3)+1);
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
      addr = NTADR_A((actor_x[1]>>3), (actor_y[1]>>3)+1);
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);

      actor_x[0] = 3*16;
      actor_y[0] = 0;
      actor_x[1] = 3*16;
      actor_y[1] = 16;
      actor_dy[0] = 1;
      actor_dy[1] = 1;
    }
    
    sprintf(str,"actor_x 0 : %d END",actor_x[0]>>4);
    vrambuf_put(NTADR_A(6,26),str,16);
    sprintf(str,"%d %d %d %d %d %d",column_height[0],column_height[1],column_height[2],column_height[3], column_height[4], column_height[5]);
    vrambuf_put(NTADR_A(6,27),str,19);      
    
    if (oam_id!=0) oam_hide_rest(oam_id);
    // ensure VRAM buffer is cleared
    ppu_wait_nmi();
    vrambuf_clear();
    //scroll(0,0);
  }
}
