
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
// RNG, from BluBlue On bayoen discord
/*dans le vrai Tsu, le jeu prend 256 puyos répartis équitablement avec les 4 couleurs de la partie, donc 64 de chaque
il les mélange en les échangeant de place
la liste qui en résulte est la suite de paires qu'on aura : les deux premiers forment la première paire, etc*/
// He has also completely deconstruct the RNG of puyo there: https://puyonexus.com/wiki/Puyo_Puyo_Tsu/Random_Number_Generator
// For my own sanity I will build my own RNG based on the principal of 4*64 colors being used 
#define NBCOLORPOOL 64
//256 puyos = 128 pairs...
//an attribute table entry is 2 bits, so 1 puyo = 2bits
//a pair of puyo = 4 bits
//with 8 bits we have 2 pairs
//So we need an array length of 64 bytes/char to stock everything, amazing !
#define PUYOLISTLENGTH 64

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
DEF_METASPRITE_2x2(puyo3, 0xd8, 2);
DEF_METASPRITE_2x2(puyo4, 0xd8, 3);

//will listed all our puyo to come, generated by RNG
char puyo_list[PUYOLISTLENGTH];
char p1_puyo_list_index;
char p2_puyo_list_index;

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

  0x26,0x20,0x16,0x00,	// background palette 0
  0x39,0x20,0x11,0x00,	// background palette 1
  0x15,0x20,0x2A,0x00,	// background palette 2
  0x12,0x20,0x28,0x00,   // background palette 3

  0x23,0x20,0x16,0x00,	// sprite palette 0
  0x2C,0x20,0x11,0x00,	// sprite palette 1
  0x28,0x20,0x2A,0x00,	// sprite palette 2
  0x2D,0x20,0x28	// sprite palette 3
};

//generates the puyo that will be used during gameplay, they will be stock into puyo_list
void generate_rng()
{
  char nb_red = NBCOLORPOOL; // palette 0
  char nb_blue = NBCOLORPOOL; // palette 1
  char nb_green = NBCOLORPOOL; // palette 2 
  char nb_yellow = NBCOLORPOOL; // palette 3
  char tmp;
  byte i, j, redo;
  // puyo_list contains 64 (PUYOLISTLENGTH) char/8 bits info
  // each 8 bits contains 2 pairs as 1 puyo is a 2 bits color
  // as we have 4 colors palettes
  // the method used here is probably attrocious in term of optimization
  // but it should works.
  // we will be using rand8(), which is fast but...not very random
  rand();
  for (i = 0 ; i < PUYOLISTLENGTH ; i++)
  {
    puyo_list[i] = 0; //reinit
    redo = 1; // loop until we get what we want;
    //get a number between 0 and 3
    for (j = 0 ; j < 4; j++)
    {
      do
      {
        tmp = rand() & 3;
        switch (tmp)
        {
          case 0:
            if (nb_red > 0)
            {
              nb_red--;
              redo = 0;
              puyo_list[i] += (tmp);
            }
            break;
          case 1:
            if (nb_blue > 0)
            {
              nb_blue--;
              redo = 0;
              puyo_list[i] += (tmp << j*2);
            }
            break;
          case 2:
            if (nb_green > 0)
            {
              nb_green--;
              redo = 0;
              puyo_list[i] += (tmp << j*2);
            }
            break;
          case 3:
            if (nb_yellow > 0)
            {
              nb_yellow--;
              redo = 0;
              puyo_list[i] += (tmp << j*2);
            }
            break;
        }
      }while (redo);
    } 
  }
}

//return the color of the sprite given by the index in parameter
//OAM is a $200, each sprites uses 4 bytes, but we use metasprites of 4 bytes
//So metasprite 0 is at 200, 1 at 210,3 at 220 etc
//The attributes also contains the flip info, so mask it to 0x3 to only have color
byte return_sprite_color(byte spr_index)
{
  OAMSprite  *spr_ptr = (OAMSprite*)(0x200+16*spr_index);
  return (spr_ptr->attr & 0x3);
}

//based on sprite x/y position look for the bg attributes related to it
//find color value based on sprite index and returned the byte with the 4 tiles
//updated with sprite color
byte return_attribute_color(byte spr_index, byte spr_x, byte spr_y)
{
  word addr = nt2attraddr(NTADR_A(spr_x,spr_y));
  byte attr_x = spr_x&0xfc;
  byte attr_y = spr_y&0xfc;
  byte attr;
  byte sprite_color = return_sprite_color(spr_index);
  byte mask = 3;
  char str[32];
  ppu_wait_nmi();//not really found about it (frame lost ?), avoid the screen blinking
  vram_adr(addr);
  vram_read(&attr,1);
  vram_adr(0x0);
  sprintf(str,"attr: %d %d",sprite_color,attr);
  vrambuf_put( NTADR_A(2,27),str,10);
  
  //we must not override colors of the tiles around the one we change
  //We must determine were our meta sprite is located in the 4*4 metatile attributes
  //if x is odd it will be on the right, even left
  //if y is odd it will be on the bottom, even top
  //LSB is top left, MSB bottom right
  if (attr_y < spr_y)
  {
    mask <<= 4;
    sprite_color <<= 4;
  }
  if (attr_x< spr_x) 
  {
    mask <<= 2;
    sprite_color <<= 2;
  }
  
  //let's erase the previous attributes for the intended position
  attr &= ~mask; //~ bitwise not, so we keep only bit outside of mask from attr
  attr += sprite_color;
  sprintf(str,"attr: %d",attr);
  vrambuf_put( NTADR_A(20,27),str,10);
  
  return attr;
}

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
    else if ( x == 8 ||x == 9 || x == 10 ||x == 11 || x == 12 || x == 13 || x == 14 || x == 15) //14 et 15
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
    else if (x == 16 || x == 17 || x == 18 || x == 19 || x == 20) // 16 et 17
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
  unsigned long toto = 1127523;
  //byte titi, tata, toto;

  setup_graphics();
  // draw message  
  vram_adr(NTADR_A(2,3));
  vram_write("HELLO, WORLD!", 12);
  build_field();
  generate_rng();
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
  p1_puyo_list_index = 0;
  //P2
  actor_x[2] = 11*16;
  actor_y[2] = 0*16;
  actor_dx[2] = 0;
  actor_dy[2] = 1;
  actor_x[3] = 11*16;
  actor_y[3] = 1*16;
  actor_dx[3] = 0;
  actor_dy[3] = 1;
  p2_puyo_list_index = 0;
    
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
      // puyoseq[0] == red, 1 blue, 2  green, 3 yellow, the good one is taken from
      // puyo_list       p1_puyo_list_index
      // (p1_puyo_list_index>>1) retourne le bon index puisqu'on a 4 paires par index
      // ensuite on décale sur le bon élément de l'index 
      // 2 bits pour chaque puyo=> on décale à droite (0<<0, 1<<2, 2<<4,3<<6)
      // et on fait & 3 pour ne garder que les 2 premiers bits
      if (i<2)
      {
        oam_id = oam_meta_spr(actor_x[i], actor_y[i], oam_id, puyoSeq[(puyo_list[(p1_puyo_list_index>>1)]>>((((p1_puyo_list_index%2)*2)+i)*2))&3]);
      }
      else
        oam_id = oam_meta_spr(actor_x[i], actor_y[i], oam_id, puyoSeq[(puyo_list[(p2_puyo_list_index>>1)]>>(i*2))&3]);
      //actor_x[i] += actor_dx[i];
      // actor_dx[i] = 0;
      actor_y[i] += actor_dy[i];
      /*if (190 < actor_y[i])
        actor_dy[i] = 0;*/
      //test relative to column_height
      if (actor_dy[i] != 0 && column_height[(actor_x[i]>>4) - 1] < actor_y[i])
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
      memset(attrbuf, 0, sizeof(attrbuf));
      
      set_metatile(0,0xd8);
      //set_attr_entry((((actor_x[0]/8)+32) & 63)/2,0,return_sprite_color(0));
      //attrbuf should take the color for 4 tiles !
      attrbuf[0] = return_attribute_color(0, actor_x[0]>>3,(actor_y[0]>>3)+1);
      set_metatile(1,0xd8);
      //set_attr_entry((((actor_x[1]/8)+32) & 63)/2,1,return_sprite_color(1));
      attrbuf[1] = return_attribute_color(1, actor_x[1]>>3, (actor_y[1]>>3)+1);/*return_sprite_color(1) + return_sprite_color(1)<<2 + return_sprite_color(1) << 4 + return_sprite_color(1) << 6*/;
      
      addr = NTADR_A((actor_x[0]>>3), (actor_y[0]>>3)+1);
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
      vrambuf_put(nt2attraddr(addr), &attrbuf[0], 1);

      addr = NTADR_A((actor_x[1]>>3), (actor_y[1]>>3)+1);
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
      vrambuf_put(nt2attraddr(addr), &attrbuf[1], 1);
      
      //test attribute
      
      /*memset(ntbuf1, 0, sizeof(ntbuf1));
      memset(ntbuf2, 0, sizeof(ntbuf2));
      memset(attrbuf, 0, sizeof(attrbuf));
      set_metatile(0, 0xd8);
      attrbuf[0] = 0x7e; //10101010 soit couleur 2 pour les 4
      addr = NTADR_A(12, 22);
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
      vrambuf_put(nt2attraddr(addr), &attrbuf[0], 1);
      
      addr = NTADR_A(14,4);
      attrbuf[1] = 0x5e;
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
      //put_attr_entries(nt2attraddr(addr));=> Non marche pas bien
      vrambuf_put(nt2attraddr(addr), &attrbuf[1], 1);
      
      addr = NTADR_A(16, 6);
      attrbuf[2] = 0x5e;
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
      //put_attr_entries(nt2attraddr(addr));=> Non marche pas bien
      vrambuf_put(nt2attraddr(addr), &attrbuf[2], 1);*/
  //rbvj

      //print state before reinit:
      sprintf(str,"pos x1:%d y1:%d x1:%d y1:%d",actor_x[0], actor_y[0], actor_x[0]>>3,(actor_y[0]>>3)+1);
      addr = NTADR_A(1,26);
      vrambuf_put(addr,str,28);
      actor_x[0] = 3*16;
      actor_y[0] = 0;
      actor_x[1] = 3*16;
      actor_y[1] = 16;
      actor_dy[0] = 1;
      actor_dy[1] = 1;
      p1_puyo_list_index ++;
    }
    
   // sprintf(str,"actory : %d_%d %d_%dEND",actor_y[0], actor_y[0]>>3, actor_y[1],actor_y[1]>>3);
    //sprintf(str,"%d %d %d %d %d %d %d %d", puyo_list[0], puyo_list[1],  puyo_list[2],  puyo_list[3],  puyo_list[4], puyo_list[5], puyo_list[6], puyo_list[7]);
    //sprintf(str,"%d %d %d %d", return_sprite_color(0),return_sprite_color(1), return_sprite_color(2), return_sprite_color(3));
    
    //sprintf(str,"actory : %lu END", toto);
    //addr = NTADR_A(2,27);
    //vrambuf_put(addr,str,24);
    //sprintf(str,"%d %d %d %d %d %d",column_height[0],column_height[1],column_height[2],column_height[3], column_height[4], column_height[5]);
    //vrambuf_put(NTADR_A(6,27),str,19);  
    //put_attr_entries(nt2attraddr(addr));
    
    if (oam_id!=0) oam_hide_rest(oam_id);
    // ensure VRAM buffer is cleared
    ppu_wait_nmi();
    vrambuf_clear();
    //scroll(0,0);
  }
}
