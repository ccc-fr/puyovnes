//custom config file, explained there http://8bitworkshop.com/blog/docs/ide.md.html#8bitworkshopideusermanual/managingfiles/cc65customconfigfiles
//it's only purpose is to allocated/defined/whatever room for the audio sample (by default DMC/SAMPLE size is too short)
#define CFGFILE puyovnes.cfg
//#resource "puyovnes.cfg"

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
//not used so commented to save space, we need space !
/*#define DEF_METASPRITE_2x2_FLIP(name,code,pal)\
const unsigned char name[]={\
        8,      0,      (code)+0,   (pal)|OAM_FLIP_H, \
        8,      8,      (code)+1,   (pal)|OAM_FLIP_H, \
        0,      0,      (code)+2,   (pal)|OAM_FLIP_H, \
        0,      8,      (code)+3,   (pal)|OAM_FLIP_H, \
        128};*/

#include "apu.h"
//#link "apu.c"

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

//Sample bayoen in asm
//#link "bayoen.s"


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
//note on CellType: PUYO_RED is first and not EMPTY for 0, because it's matching the attribute table
//(I think I will regret that decision later...)
typedef enum CellType {PUYO_RED, PUYO_BLUE, PUYO_GREEN, PUYO_YELLOW, OJAMA, EMPTY, PUYO_POP};
typedef enum Step {SETUP, PLAY, CHECK, CHECK_ALL, DESTROY, FALL, POINT, SHOW_NEXT, FALL_OJAMA, FLUSH, WAIT};
/*byte seg_height;	// segment height in metatiles
byte seg_width;		// segment width in metatiles*/
byte seg_char;		// character to draw
byte seg_palette;	// attribute table value

byte current_player; // takes 0 or 1

//byte step_p1, step_p2;
//byte step_p1_counter, step_p2_counter;// indicates where we are in the process.
//we will use array for these new to easily switch from p1 to p2
byte step_p[2];
byte step_p_counter[2];

// attribute table in PRG ROM
byte attribute_table[64];/* = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rows 0-3
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rows 4-7
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rows 8-11
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rows 12-15
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rows 16-19
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rows 20-23
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rows 24-27
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // rows 28-29
};*/
//both play tables can be stored in a 6*13 array of byte
//4 LSB are for board one, value car by of type CellType (1 bit unused), so must use &0xf to get correct value 
//4 MSB are for 2nd board, so must use >>4 to get correct value, or even better (value&240)>>4, probably overkill
//and still 1 bit unused.
//[0][0] is top left, [5][12] is bottom right, keeping same axis than display for simplification
//byte boards[6][13];

//Ok stop the bit shift thing it's horrendous, let's go simple : each board his table.
//yes we lose 78 bytes of memory, but it will be simpler to manage and less cycle consuming
byte boards[2][6][13];

//for sake of simplicity keeping the same table type for tmp
//but we may look for something less huge in size later.
//we bump tmp_boards to 15 for the flush step, will contain what was the floor before
byte tmp_boards[6][15];

// buffers that hold vertical slices of nametable data
char ntbuf1[PLAYROWS];	// left side
char ntbuf2[PLAYROWS];	// right side

// a vertical slice of attribute table entries
char attrbuf[PLAYROWS/4];

// nos sprites de puyo
DEF_METASPRITE_2x2(puyo_red, 0xd8, 0);//red
DEF_METASPRITE_2x2(puyo_blue, 0xd8, 1);//blue
DEF_METASPRITE_2x2(puyo_green, 0xd8, 2);//green
DEF_METASPRITE_2x2(puyo_yellow, 0xd8, 3);//yellow
DEF_METASPRITE_2x2(puyo_heart, 0xcc, 0);//red
DEF_METASPRITE_2x2(puyo_rabbit_ghost, 0xd4, 1);//blue
DEF_METASPRITE_2x2(puyo_angry, 0xd0, 2);//green
DEF_METASPRITE_2x2(puyo_yellow_bis, 0xd8, 3);//yellow
//not needed
/*DEF_METASPRITE_2x2(ojama, 0xdc, 0);
DEF_METASPRITE_2x2(puyo_pop, 0xe0, 0);*/

//will list all our puyo to come, generated by RNG
byte puyo_list[PUYOLISTLENGTH];
byte p_puyo_list_index[2];

// actor x/y positions
byte actor_x[2][2];
byte actor_y[2][2];
// actor x/y deltas per frame (signed)
sbyte actor_dx[2][2];
sbyte actor_dy[2][2];

//Variables for damages 
//cf https://www.bayoen.fr/wiki/Tableau_des_dommages for how to compute damages.
byte nb_puyos_destroyed[2]; //how many puyos are destroyed on that hit
byte nb_hit[2];// hit combo counter
byte mask_color_destroyed; // LSB p1, MSB p2, bit mask at 1 for each color present in the hit. bit 0 red, bit 1 blue, bit 2 green, 3 yellow 
byte nb_group[2];//if the group is over 4 puyos add the part over in this variable.
unsigned long int score[2]; //current score of the round
byte wins[2]; // number of round won by each player. 
byte ready[2]; //indicates if a player is ok to play the next round
unsigned long int ojamas[4];// 2 pockets of ojama per player, but what is displayed is always the sum of both. Warikomi rule.
byte step_ojama_fall[2];
byte should_destroy;
byte blind_offset; //offset to apply to get the correct sprite
byte sprite_addr[2][2]; //keep track of the addr of the sprite tile to convert from sprite to tile

//INPUT BUFFER DELAY
#define INPUT_DIRECTION_DELAY 4
#define INPUT_BUTTON_DELAY 4
//previously declared in main()
char oam_id;	// sprite ID
char pad;	// controller flags
char previous_pad[2];
char input_delay_PAD_LEFT[2]; //to avoid multiple input on one press
char input_delay_PAD_RIGHT[2]; //to avoid multiple input on one press
char input_delay_PAD_A[2]; //to avoid multiple input on one press
char input_delay_PAD_B[2]; //to avoid multiple input on one press
//char column_height[12]; // heigth of the stack, 0 to 5 p1, 6 to 11 P2, may not be the best strategy =>indeed, changing for double table
char column_height[2][6];
char column_height_offset;
//constant for puyo physics
#define GRACE_PERIOD 32
#define MAX_FALLING_BACK_UP 8
byte timer_grace_period[2];
byte counter_falling_back_up[2];
#define FLAG 8
byte bg_tile;//address of top left, bottom left+1, top right+2, bottom right+4
byte bg_pal;//0 color palette 0, 85 color palette 1 (4 couleur par octets, 0b01010101), 170 color palette 2 0b10101010, 255 color palette 3 0b11111111
byte menu_pos_x;
byte menu_pos_y[4];

//
// MUSIC ROUTINES
//

// Namespace(bias=1.0, freq=111860.8, length=64, maxbits=13.0, upper=41)
// 439.0 0.943191918851 41
/*const int note_table_41[64] = {
4318, 4076, 3847, 3631, 3427, 3235, 3053, 2882, 2720, 2567, 2423, 2287, 2159, 2037, 1923, 1815, 1713, 1617, 1526, 1440, 1360, 1283, 1211, 1143, 1079, 1018, 961, 907, 856, 808, 763, 720, 679, 641, 605, 571, 539, 509, 480, 453, 428, 403, 381, 359, 339, 320, 302, 285, 269, 254, 240, 226, 213, 201, 190, 179, 169, 160, 151, 142, 134, 126, 119, 113, };
*/
// Namespace(bias=1.0, freq=111860.8, length=64, maxbits=13, upper=49)
// 440.5 1.79281159771 49
const int note_table_49[64] = {
4304, 4062, 3834, 3619, 3416, 3224, 3043, 2872, 2711, 2559, 2415, 2279, 2151, 2031, 1917, 1809, 1707, 1611, 1521, 1436, 1355, 1279, 1207, 1139, 1075, 1015, 958, 904, 853, 805, 760, 717, 677, 639, 603, 569, 537, 507, 478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253, 239, 225, 213, 201, 189, 179, 168, 159, 150, 142, 134, 126, 119, 112, };

// Namespace(bias=1.0, freq=111860.8, length=64, maxbits=12, upper=63)
// 443.6 14.2328382554 63
/*const int note_table_63[64] = {
2137, 4034, 3807, 3593, 3392, 3201, 3022, 2852, 2692, 2541, 2398, 2263, 2136, 2016, 1903, 1796, 1695, 1600, 1510, 1425, 1345, 1270, 1199, 1131, 1068, 1008, 951, 898, 847, 800, 755, 712, 672, 634, 599, 565, 533, 503, 475, 448, 423, 399, 377, 356, 336, 317, 299, 282, 266, 251, 237, 224, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118, 111, };
*/
// Namespace(bias=-1.0, freq=55930.4, length=64, maxbits=12, upper=53)
// 443.7 8.47550713772 53
const int note_table_tri[64] = {
2138, 2018, 1905, 1798, 1697, 1602, 1512, 1427, 1347, 1272, 1200, 1133, 1069, 1009, 953, 899, 849, 801, 756, 714, 674, 636, 601, 567, 535, 505, 477, 450, 425, 401, 379, 358, 338, 319, 301, 284, 268, 253, 239, 226, 213, 201, 190, 179, 169, 160, 151, 142, 135, 127, 120, 113, 107, 101, 95, 90, 85, 80, 76, 72, 68, 64, 60, 57, };


#define NOTE_TABLE note_table_49
#define BASS_NOTE /*36*/ 12

byte music_index = 0;
byte cur_duration = 0;

const byte music1[]; // music data -- see end of file
const byte* music_ptr = music1;
#define SAMPLE_TEST 0xF800
//const byte bayoen[];
//extern const void * bayoen_sample_data[];

//end of music bloc

//le const machin const permet de placer l'info en rom et donc de gagner de la place en théorie
const unsigned char* const puyoSeq[8] = {
  puyo_red, puyo_blue, puyo_green, puyo_yellow, puyo_heart, puyo_rabbit_ghost, puyo_angry, puyo_yellow_bis
};
//1:    0xfc   ojama
//6:    0xf8   big ojama
//30:   0xe4   rock
//180:  0xe8   tilted rock
//360:  0xec   star
//720:  0xf0   crown
//1440: 0xf4   comet
const unsigned int const damageList[7] = 
{ 
  1440,720,360,180,30,6,1
};

const byte const damageTile[7] = 
{ 
  0xf4,0xf0,0xec,0xe8,0xe4,0xf8,0xfc
};

const byte const start_pos_x[2] = 
{
  3*16,11*16
};

const byte const start_pos_y[2][2] =
{
 {0,16},
 {0,16}
};
//When translating x position (from 0 to ~256) into column index we >>4 and remove the below offset 
const byte const pos_x_offset[2] = {1,9};
//nametable offset, useful for update tile/nametable/attributes
const byte const nt_x_offset[2] = {2,18};
//shift for color table
const byte const shift[2] = {0,4};


//declaration des fonctions, il parait que ça aide
word nt2attraddr(word a);
void set_metatile(byte y, byte ch);
void clear_metatile(byte y);
void set_attr_entry(byte x, byte y, byte pal);
void put_attr_entries(word addr, byte length);
void generate_rng(void);
byte return_sprite_color(byte spr_index);
byte return_tile_attribute_color(byte color, byte spr_x, byte spr_y);
void update_boards(void); //return if a puyo has been placed on x = 2 and y = 1, then flush
byte check_board(byte x, byte y);
byte destroy_board(void);
void fall_board(void);
void manage_point(void);
void build_field(void);
void setup_graphics(void);
void handle_controler_and_sprites(void);
void update_next(void);
//music and sfx related
byte next_music_byte(void);
void play_music(void);
void start_music(const byte* music);
void play_rotation_sound(void);
void play_hit(byte hit); //pitch get higher with byte until reaching "bayoen !"
void play_puyo_fix(void); //when puyo is hitting ground, to be changed a bit tamed currently
void play_bayoen(void); // play bayoen sample
void play_flush(void); // flush sound when a player lose
byte fall_ojama(void); //fall ojama damage on the player field
void flush(void); // flush loser screen into under the play field
void init_round(void); //set actors, column_height and other things before a round
void put_str(unsigned int adr, const char *str);

//music bloc definition
byte next_music_byte() {
  return *music_ptr++;
}

void play_music() {
  static byte chs = 0;
  if (music_ptr) {
    // run out duration timer yet?
    while (cur_duration == 0) {
      // fetch next byte in score
      byte note = next_music_byte();
      // is this a note?
      if ((note & 0x80) == 0) {
        // pulse plays higher notes, triangle for lower if it's free
        if (note >= BASS_NOTE || (chs & 4)) {
          int period = NOTE_TABLE[note & 63];
          // see which pulse generator is free
          if (!(chs & 1)) {
            APU_PULSE_DECAY(0, period, DUTY_25, 2, 10);
            chs |= 1;
          } else if (!(chs & 2)) {
            APU_PULSE_DECAY(1, period, DUTY_25, 2, 10);
            chs |= 2;
          }
        } else {
          int period = note_table_tri[note & 63];
          APU_TRIANGLE_LENGTH(period, 15);
          chs |= 4;
        }
      } else {
        // end of score marker
        if (note == 0xff)
          music_ptr = NULL;
        // set duration until next note
        cur_duration = note & 63;
        // reset channel used mask
        chs = 0;
      }
    }
    cur_duration--;
  }
}

void start_music(const byte* music) {
  music_ptr = music;
  cur_duration = 0;
}

//sfx
void play_rotation_sound()
{
  //APU_ENABLE(ENABLE_NOISE);
  APU_NOISE_SUSTAIN(15,2);
  APU_NOISE_DECAY(3,8,2);
}

void play_hit(byte hit)
{
  //APU_ENABLE(ENABLE_NOISE);
  //PULSE_CH0 is used by music, the sweep can be an issue
  //as it won't be deactivated automatically
  //so we use PULSE_CH1 for the moment
  switch (hit)
  {
    case 0:
      APU_PULSE_DECAY(PULSE_CH1, 2000, 192, 8, 1);
      break;
    case 1:
      APU_PULSE_DECAY(PULSE_CH1, 1750, 192, 8, 1);
      break;
    case 2:
      APU_PULSE_DECAY(PULSE_CH1, 1200, 192, 8, 1);
      break;
    case 3:
      APU_PULSE_DECAY(PULSE_CH1, 900, 192, 8, 1);
      break;
    case 4:
      APU_PULSE_DECAY(PULSE_CH1, 600, 192, 8, 1);
      break;
    case 5:
      APU_PULSE_DECAY(PULSE_CH1, 450, 192, 8, 1);
      break;
    case 6:
      APU_PULSE_DECAY(PULSE_CH1, 200, 192, 8, 1);
      break;
    case 7:
      APU_PULSE_DECAY(PULSE_CH1, 150, 192, 8, 1);
      break;
    case 8:
      APU_PULSE_DECAY(PULSE_CH1, 100, 192, 8, 1);
      break;
    default:
      play_bayoen();
      return;
  }
  
  //APU_PULSE_DECAY(PULSE_CH1, /*1121*/750+((hit-1)<<7), 192, 8, 1);
  APU_PULSE_SWEEP(PULSE_CH1,4,2,1);
  APU_NOISE_DECAY(0,8,3);
  //APU_PULSE_SWEEP_DISABLE(PULSE_CH0);
}

void play_puyo_fix()
{
  APU_TRIANGLE_LENGTH(497,13);
}


void play_bayoen()
{
  APU_ENABLE(0x1f);//DMC channel must be enabled each time a sample is played, because...
  APU_DMC_CONTROL(0x0E);//E 24khz, 0e sans int, 8e avec interruption
  APU_DMC_OUTPUT(0x3f); //3f value given by DMCConv.exe
  APU_DMC_address(SAMPLE_TEST);
  APU_DMC_length(0x75/*0xf*/);
}

void play_flush()
{
  APU_PULSE_DECAY(PULSE_CH1, 963, 128, 10, 1);
  APU_PULSE_SWEEP(PULSE_CH1,6,3,0);
  //APU_PULSE_DECAY(PULSE_CH1, 476, 192, 10, 12);
  //APU_PULSE_SWEEP(PULSE_CH1,2,0,1);
  APU_TRIANGLE_LENGTH(1735,4);
  APU_NOISE_DECAY(10,7,128);
}

//end of music bloc

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

void clear_metatile(byte y)
{
  ntbuf1[y*2] = 0;
  ntbuf1[y*2+1] = 0;
  ntbuf2[y*2] = 0;
  ntbuf2[y*2+1] = 0;
}

// set attribute table entry in attrbuf
// x and y are metatile coordinates
// pal is the index to set
void set_attr_entry(byte x, byte y, byte pal) {
  if (y&1) pal <<= 4;
  if (x&1) pal <<= 2;
  attrbuf[y/2] |= pal;
}

void put_attr_entries(word addr, byte length) {
  byte i;
  for (i=0; i<length; ++i) {
    VRAMBUF_PUT(addr, attrbuf[i], 0);
    addr += 8;
  }
  vrambuf_end();
}

void put_str(unsigned int adr, const char *str)
{
  vram_adr(adr); //set PPU read/write address
  vram_write(str, strlen(str));//write bytes to PPU
}

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x0D,			// screen color

  0x24,0x20,0x16,0x00,	// background palette 0
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
  byte nb_red = NBCOLORPOOL; // palette 0
  byte nb_blue = NBCOLORPOOL; // palette 1
  byte nb_green = NBCOLORPOOL; // palette 2 
  byte nb_yellow = NBCOLORPOOL; // palette 3
  char tmp;
  byte i, j, redo;
  // puyo_list contains 64 (PUYOLISTLENGTH) char/8 bits info
  // each 8 bits contains 2 pairs as 1 puyo is a 2 bits color
  // as we have 4 colors palettes
  // the method used here is probably attrocious in term of optimization
  // but it should works.
  // we will be using rand8(), which is fast but...not very random
  rand();
  for (i = 0 ; i < PUYOLISTLENGTH ; ++i)
  {
    puyo_list[i] = 0; //reinit
    redo = 1; // loop until we get what we want;
    //get a number between 0 and 3
    for (j = 0 ; j < 4; ++j)
    {
      do
      {
        tmp = rand() & 3;
        switch (tmp)
        {
          case 0:
            if (nb_red > 0)
            {
              --nb_red;
              redo = 0;
              puyo_list[i] += (tmp);
            }
            break;
          case 1:
            if (nb_blue > 0)
            {
              --nb_blue;
              redo = 0;
              puyo_list[i] += (tmp << j*2);
            }
            break;
          case 2:
            if (nb_green > 0)
            {
              --nb_green;
              redo = 0;
              puyo_list[i] += (tmp << j*2);
            }
            break;
          case 3:
            if (nb_yellow > 0)
            {
              --nb_yellow;
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
//So metasprite 0 is at $200, 1 at $210,3 at $220 etc
//The attributes also contains the flip info, so mask it to 0x3 to only have color
//see https://wiki.nesdev.com/w/index.php/PPU_OAM, byte 0 Y, byte 1 tile index, byte 2 attributes, byte 4 X
byte return_sprite_color(byte spr_index)
{
  OAMSprite  *spr_ptr = (OAMSprite*)(0x200+16*spr_index);
  return (spr_ptr->attr & 0x3);
}

//based on sprite x/y position look for the bg attributes related to it
//then update the attributes with the color passes in parameter
byte return_tile_attribute_color(byte color, byte spr_x, byte spr_y)
{
 //word addr = nt2attraddr(NTADR_A(spr_x,spr_y));
  byte attr_x = spr_x&0xfc;
  byte attr_y = spr_y&0xfc;
  byte attr;
  byte index;
  byte mask = 3;
  //we must not override colors of the tiles around the one we change
  //We must determine were our meta sprite is located in the 4*4 metatile attributes
  //if x is odd it will be on the right, even left
  //if y is odd it will be on the bottom, even top
  //LSB is top left, MSB bottom right
  if (attr_y < spr_y)
  {
    mask <<= 4;
    color <<= 4;
  }
  if (attr_x < spr_x) 
  {
    mask <<= 2;
    color <<= 2;
  }
  // attribute position is y/2 + x/4 where y 2 LSB are 0
  index = (attr_y<<1) + (spr_x>>2);

  attr = attribute_table[index];
  //let's erase the previous attributes for the intended position
  attr &= ~mask; //~ bitwise not, so we keep only bit outside of mask from attr
  attr += color;
  attribute_table[index] = attr;
  return attr;
}

//Update the boards table, once the puyos have stop moving, not optimized :-S
//board_index must take 0 or 2=>Not anymore, changed to match current_player elsewhere !
void update_boards()
{
  //byte x,y;// variable are slow and not necessary here
  //column 0 is at 2 for actor_x[board_index]>>3, it gives us an offset, and we need to divide by 2 after to 
  //get the column number right
  
  //column 0 is at 18 for actor_x[board_index]>>3, it gives us an offset, and we need to divide by 2 after to 
  //byte offset = (current_player == 0) ? 2 : 18;// replaced by global nt_x_offset
  //char str[32];
  
  //we must be careful not to erase the data for p2 table
  //column 0 is at 2 for actor_x[board_index]>>3, it gives us an offset, and we need to divide by 2 after to 
  //get the column number right
  
  //x = ((actor_x[current_player][0]>>3) - nt_x_offset[current_player]) >> 1;
  //y = ((actor_y[current_player][0]>>3)+1)>>1;
  boards[current_player][((actor_x[current_player][0]>>3) - nt_x_offset[current_player]) >> 1][((actor_y[current_player][0]>>3)+1)>>1] = return_sprite_color(current_player<<1);

  //x = ((actor_x[current_player][1]>>3) - nt_x_offset[current_player]) >> 1;
  //y = ((actor_y[current_player][1]>>3)+1)>>1;
  boards[current_player][((actor_x[current_player][1]>>3) - nt_x_offset[current_player]) >> 1][((actor_y[current_player][1]>>3)+1)>>1] = return_sprite_color((current_player<<1)+1);

  return;
}

// Look for puyo to destroy and flag them as such
byte check_board(/*byte board_index,*/ byte x, byte y)
{
  static byte i, j, k, current_color; //static are faster, but they are keeping there value outside of context
  byte counter = 0, tmp_counter = 0;
  /*byte mask = 15, flag = 8, shift = 0;*/
  byte destruction = 0;
  //char str[32];
  
  //to gain time we start from position of the last placed puyos
  //actor_x[board_index], actor_y[board_index],actor_x[board_index+1], actor_y[board_index+1],
  //x,y for each last puyo should be save somehere to gain time
  /*x = ((actor_x[board_index]>>3) - 2) >> 1;
  y = ((actor_y[board_index]>>3)+1)>>1;*/
  // First find puyo of the same color up, down, left, right to the current one.=, if no found exit
  // then move to the line above and below and check for a puyo of same color nearby
  // raise counter
  // until no more puyo left.
  // redo for next puyo
  // that way contrary to previous method we should not miss some boom.
  // Note : how to do when after destruction ?
  //k == current line above, k == current line below
  
  memset(tmp_boards,0,sizeof(tmp_boards));

  current_color = ((boards[current_player][x][y]));

  //tmp_boards contains flag of the currently looked color 
  tmp_boards[x][y] = FLAG;
  i = (x - 1); //byte are unsigned, so -1 = 255, we will not enter in the while if i < 0
  while ( i < 6 )
  {
    if ( tmp_boards[i][y] != FLAG)
    {
      if (current_color == ((boards[current_player][i][y]) ))
      {     
        tmp_boards[i][y] = FLAG;
        ++counter;
      }
      else
      {  
       /* i = 7; //no need to continue if not found
        continue;*/
        break; //sort du while
      }
    }
    --i;
  }
  
  i = (x + 1);
  while ( i < 6 )
  {
    if ( tmp_boards[i][y] != FLAG)
    {
      if (current_color == ((boards[current_player][i][y]) ))
      {     
        tmp_boards[i][y] = FLAG;
        ++counter;
      }
      else
      {
        /*i = 7; //no need to continue if not found
        continue;*/
        break;
      }
    }
    ++i;
  }
  
  i = (y - 1);
  while ( i < 13 )
  {
    if ( tmp_boards[x][i] != FLAG)
    {
      if (current_color == ((boards[current_player][x][i]) ))
      {     
        tmp_boards[x][i] = FLAG;
        ++counter;
      } 
      else
      {
        //i = 14; //no need to continue if not found
        break;
      }
    }
    --i;
  }
  
  i = (y + 1);
  while ( i < 13 )
  {
    if ( tmp_boards[x][i] != FLAG)
    {
      if (current_color == ((boards[current_player][x][i]) ))
      {     
        tmp_boards[x][i] = FLAG;
        ++counter;
      } 
      else
      {
        //i = 14; //no need to continue if not found
        break;
      }
    }
    ++i;
  }
  //nothing found ? exit !
  if (counter == 0)
    return 0;
  
  //ok so we got something, now looking for more
  j = (y-1);
  //we go above, so we look below, left and right
  //we must do the line in both way (0 to 6 and 6 to 0) to avoid missing something
  while (j < 13)
  {
    //for not testing under or over the board
    k = j+1;
    //unlooped version
    //0
    if (tmp_boards[0][j] != FLAG && (current_color == ((boards[current_player][0][j]) )) && 
        ( ((k!=13) ? (tmp_boards[0][k] == FLAG) : false) ||
         (tmp_boards[1][j] == FLAG)) )
    {
      tmp_boards[0][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //1
    if ( tmp_boards[1][j] != FLAG && (current_color == ((boards[current_player][1][j]) )) && 
        ( ((k!=13) ? (tmp_boards[1][k] == FLAG) : false) ||
         (tmp_boards[0][j] == FLAG) ||
         (tmp_boards[2][j] == FLAG)) )
    {
      tmp_boards[1][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //2
    if ( tmp_boards[2][j] != FLAG &&  (current_color == ((boards[current_player][2][j]))) && 
        ( ((k!=13) ? (tmp_boards[2][k] == FLAG) : false) ||
         (tmp_boards[1][j] == FLAG) ||
         (tmp_boards[3][j] == FLAG)) )
    {
      tmp_boards[2][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //3
    if ( tmp_boards[3][j] != FLAG &&  (current_color == ((boards[current_player][3][j] ) )) && 
        ( ((k!=13) ? (tmp_boards[3][k] == FLAG) : false) ||
         (tmp_boards[2][j] == FLAG) ||
         (tmp_boards[4][j] == FLAG)) )
    {
      tmp_boards[3][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //4
    if ( tmp_boards[4][j] != FLAG &&  (current_color == ((boards[current_player][4][j]))) && 
        ( ((k!=13) ? (tmp_boards[4][k] == FLAG) : false) ||
         (tmp_boards[3][j] == FLAG) ||
         (tmp_boards[5][j] == FLAG)) )
    {
      tmp_boards[4][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //5
    if ( tmp_boards[5][j] != FLAG && (current_color == ((boards[current_player][5][j] ))) && 
        ( ((k!=13) ? (tmp_boards[5][k] == FLAG) : false) ||
         (tmp_boards[4][j] == FLAG)))
    {
      tmp_boards[5][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }
    
    //no need to go backward if nothing has been foun in the first loop
    if (tmp_counter != 0) 
    {
      
      //4
      if ( tmp_boards[4][j] != FLAG && (current_color == ((boards[current_player][4][j]))) && 
           ((tmp_boards[3][j] == FLAG) ||
            (tmp_boards[5][j] == FLAG)) )
      {
        tmp_boards[4][j] = FLAG;
        ++counter;
      }
      
      //3
      if ( tmp_boards[3][j] != FLAG && (current_color == ((boards[current_player][3][j] ) )) && 
           ((tmp_boards[2][j] == FLAG) ||
            (tmp_boards[4][j] == FLAG)) )
      {
        tmp_boards[3][j] = FLAG;
        ++counter;
      }
      
      //2
      if ( tmp_boards[2][j] != FLAG && (current_color == ((boards[current_player][2][j] ) )) && 
           ((tmp_boards[1][j] == FLAG) ||
            (tmp_boards[3][j] == FLAG)) )
      {
        tmp_boards[2][j] = FLAG;
        ++counter;
      }
      
      //1
      if ( tmp_boards[1][j] != FLAG && (current_color == ((boards[current_player][1][j] ) )) && 
           ((tmp_boards[0][j] == FLAG) ||
            (tmp_boards[2][j] == FLAG)) )
      {
        tmp_boards[1][j] = FLAG;
        ++counter;
      }
      
       //0
      if ( tmp_boards[0][j] != FLAG && (current_color == ((boards[current_player][0][j] ) )) && 
           ((tmp_boards[1][j] == FLAG)) )
      {
        tmp_boards[0][j] = FLAG;
        ++counter;
      } 
      
    }
    tmp_counter = 0;
    --j; //going above is getting lower j
  }
  
  j = y+1;
  //we go below so we look above
  while (j < 13)
  {
  
    //for not testing under or over the board
    k = j-1;
    
    //unlooped version
    //0
    if (tmp_boards[0][j] != FLAG && (current_color == ((boards[current_player][0][j]) )) && 
        (((k<13) ? (tmp_boards[0][k] == FLAG) : false) ||
         (tmp_boards[1][j] == FLAG)) )
    {
      tmp_boards[0][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //1
    if ( tmp_boards[1][j] != FLAG && (current_color == ((boards[current_player][1][j] ) )) && 
        (((k<13) ? (tmp_boards[1][k] == FLAG) : false) ||
         (tmp_boards[0][j] == FLAG) ||
         (tmp_boards[2][j] == FLAG)) )
    {
      tmp_boards[1][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //2
    if ( tmp_boards[2][j] != FLAG &&  (current_color == ((boards[current_player][2][j] ) )) && 
        (((k<13) ? (tmp_boards[2][k] == FLAG) : false) ||
         (tmp_boards[1][j] == FLAG) ||
         (tmp_boards[3][j] == FLAG)) )
    {
      tmp_boards[2][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //3
    if ( tmp_boards[3][j] != FLAG &&  (current_color == ((boards[current_player][3][j] ) )) && 
        (((k<13) ? (tmp_boards[3][k] == FLAG) : false) ||
         (tmp_boards[2][j] == FLAG) ||
         (tmp_boards[4][j] == FLAG)) )
    {
      tmp_boards[3][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //4
    if ( tmp_boards[4][j] != FLAG &&  (current_color == ((boards[current_player][4][j]) )) && 
        (((k<13) ? (tmp_boards[4][k] == FLAG) : false) ||
         (tmp_boards[3][j] == FLAG) ||
         (tmp_boards[5][j] == FLAG)) )
    {
      tmp_boards[4][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    //5
    if ( tmp_boards[5][j] != FLAG && (current_color == ((boards[current_player][5][j]) )) && 
        ( ((k<13) ? (tmp_boards[5][k] == FLAG) : false) ||
         (tmp_boards[4][j] == FLAG)))
    {
      tmp_boards[5][j] = FLAG;
      ++counter;
      ++tmp_counter;
    }

    if (tmp_counter != 0)
    {
      
      //5 useless, same as last from previous "loop"
      /*if ( tmp_boards[5][j] != flag && (current_color == ((boards[5][j] & mask) >> shift)) && 
           (tmp_boards[4][j] == flag))
      {
        tmp_boards[5][j] = flag;
        ++counter;
      }*/
      
      //4
      if ( tmp_boards[4][j] != FLAG && (current_color == ((boards[current_player][4][j]) )) && 
           ((tmp_boards[3][j] == FLAG) ||
            (tmp_boards[5][j] == FLAG)) )
      {
        tmp_boards[4][j] = FLAG;
        ++counter;
      }
      
      //3
      if ( tmp_boards[3][j] != FLAG && (current_color == ((boards[current_player][3][j] ) )) && 
           ((tmp_boards[2][j] == FLAG) ||
            (tmp_boards[4][j] == FLAG)) )
      {
        tmp_boards[3][j] = FLAG;
        ++counter;
      }
      
      //2
      if ( tmp_boards[2][j] != FLAG && (current_color == ((boards[current_player][2][j]) )) && 
           ((tmp_boards[1][j] == FLAG) ||
            (tmp_boards[3][j] == FLAG)) )
      {
        tmp_boards[2][j] = FLAG;
        ++counter;
      }
      
      //1
      if ( tmp_boards[1][j] != FLAG && (current_color == ((boards[current_player][1][j]))) && 
           ((tmp_boards[0][j] == FLAG) ||
            (tmp_boards[2][j] == FLAG)) )
      {
        tmp_boards[1][j] = FLAG;
        ++counter;
      }
      
       //0
      if ( tmp_boards[0][j] != FLAG && (current_color == ((boards[current_player][0][j]))) && 
           ((tmp_boards[1][j] == FLAG)) )
      {
        tmp_boards[0][j] = FLAG;
        ++counter;
      } 
    }
    tmp_counter = 0;
    ++j; //going below is getting higher j
  }
  
  //we started from 0, so at 3 we have 4 to erase
  if (counter >= 3)
  {
    //update the variable for point counting
    nb_puyos_destroyed[current_player] += (counter + 1); //how many puyos are destroyed on that hit
    // LSB p1, MSB p2, bit mask at 1 for each color present in the hit. bit 0 red, bit 1 blue, bit 2 green, 3 yellow
    mask_color_destroyed |= (1 << shift[current_player]) << current_color;  
    nb_group[current_player] += (counter + 1) - 4;//if the group is over 4 puyos add the part over in this variable.

    //copy flag to boards
    for (i = 0; i < 6; ++i)
    {
      for (j = 12 ; j <= 12 ; --j)
      {
        if ( tmp_boards[i][j] == FLAG)
        {
          boards[current_player][i][j] |= FLAG;
          ++destruction;
        }
      }
    }
  }
  return destruction;
}

//puyo visual destroying after check_board
byte destroy_board()
{
  byte i, j;
  byte tmp_counter;
  register word addr;
  byte counter = 0, tmp_line = 0;
  byte destruction = 0;
 
  //char str[32];
  
  tmp_counter = step_p_counter[current_player];
  
  //step 0 we change the puyo to puyo_pop => e0
  if (tmp_counter < 6)
  {
    memset(ntbuf1, 0, sizeof(ntbuf1));
    memset(ntbuf2, 0, sizeof(ntbuf2));
    //memset(attrbuf, 0, sizeof(attrbuf)); // no need to reset the attributes, they are not changed !

    set_metatile(0,0xe0); //0xe0 == puyo_pop

    for (j = 0; j < 13 ; ++j)
    {
      if ((boards[current_player][tmp_counter][j] & FLAG) > 0)
      {
        //(i+1)<<1 à l'air ok, y on y est pas encore
        //addr = NTADR_A((i+1)<<1, j *2 );//?????
        addr = NTADR_A(((tmp_counter)*2) + nt_x_offset[current_player], j << 1 );//?????
        vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
        vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
        /*addr = NTADR_A(i, (j)+2);
          vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
          vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);*/
        //Sujet traité, on zappe le flag
        //boards[current_player][tmp_counter][j] -= FLAG;
        //On change le board pour puyo_pop
        //boards[current_player][tmp_counter][j] = PUYO_POP;
        //We will reuse the flag for destruction
        //boards[current_player][tmp_counter][j] += FLAG;
        
        //sum-up the 3 lines into 1 !
        boards[current_player][tmp_counter][j] = PUYO_POP + FLAG;

        tmp_line=j;
      }
    }
    
  }
  //step 1 we change the puyo_pop to nothing
  if (tmp_counter >= 12)
  {
    memset(ntbuf1, 0, sizeof(ntbuf1));
    memset(ntbuf2, 0, sizeof(ntbuf2));
    //memset(attrbuf, 0, sizeof(attrbuf));

    clear_metatile(0);
    i = tmp_counter%12;
    
    for (j = 0; j < 13 ; ++j)
    {
      if ((boards[current_player][i][j] & FLAG) > 0)
      {
        addr = NTADR_A(((i)*2) + nt_x_offset[current_player], j << 1 );
        vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
        vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
        //sujet traité, on zappe le flag
        //boards[current_player][i][j] -= FLAG; //useless to remove the flag as it will be removed by = EMPTY...
        //on change le board pour EMPTY
        boards[current_player][i][j] = EMPTY;
      }
    }

    if (tmp_counter == 17)
    {
      step_p[current_player] = POINT;
      step_p_counter[current_player] = 255;
    }
  }

  ++step_p_counter[current_player];
  return 0;
}

/*puyo falling*/
void fall_board()
{
  //TODO !
  //column_height doit être baissé (enfin montée, 0 est en haut) de 16 (+=16) à chaque fois qu'on descend un truc
  //on part d'en haut, de la première colonne, et on descend
  //step_p1_counter donne la colonne
  //on reconstruit d'abord la colonne sans trou dans boards[x][y]
  //ensuite on redessine toute la colonne dans le buffer.
  //si pas de changement on ne fait rien pour gagner en temps de calcul !
  byte j, j2;
  register word addr;
  byte can_fall = 0, previous_empty = 0, puyo_found = 0;
  byte smask = 7;
  byte attr_x_shift = 1;
  byte fall = 0;
  byte tmp_counter = 0, tmp_counter_2 = 0, tmp_counter_3 = 0;
  //char str[32];
   
  tmp_counter = step_p_counter[current_player]%6; /*step_p1_counter%6;*/

  
  for (j = 0 ; j < 13 ; ++j)
  {
    if (can_fall != 1 && ( (boards[current_player][tmp_counter][j] & smask)) != EMPTY)
    {
      puyo_found = j;// if no puyo are found then the column is empty=> need to reset height
      //as long as no puyo is found, there is nothing to get down
      can_fall = 1;
      if (j+1 < 13)
        ++j;  
    }

    if (can_fall == 1 && ( (boards[current_player][tmp_counter][j] & smask)) == EMPTY)
    {
      //this is where things get interesting, lets move everything down.
      //we start from j and get up to avoid overwriting values
      for (j2 = j ; j2 >= previous_empty && j2 < 255 ; --j2)
      {
        if (j2 == 0) 
          boards[current_player][tmp_counter][j2] = EMPTY; /*(boards[board_index][tmp_counter][j2] & invmask) + (EMPTY << shift);*/
        else
          boards[current_player][tmp_counter][j2] = boards[current_player][tmp_counter][j2-1]; /*(boards[board_index][tmp_counter][j2] & invmask) + (boards[board_index][tmp_counter][j2-1] & mask);  */
      }
      fall = 1;
      /*sprintf(str,"F %d", tmp_counter);
        vrambuf_put(NTADR_A(16,15+tmp_counter),str,8);*/

      //careful we wan't to only fall of 1 puyo height per cycle !
      //So we keep the position of the last element that has falled so top there
      previous_empty = j+1;
      can_fall = 0;
    }
  }
 
  if (fall == 1)
  {
    //If we got a fall we reset the counter, then ...?
    //if (step_p[current_player] != FALL_OJAMA)
    step_p_counter[current_player] = tmp_counter;
    //As it fall the height of the column must be lowered:
    if (column_height[current_player][tmp_counter] < 190) //let's avoid going under the floor
      column_height[current_player][tmp_counter] +=16;
    
    if (current_player != 0)
    {
      tmp_counter_2 = (tmp_counter + 9) << 1;
      tmp_counter_3 = tmp_counter + 8;
    }
    else
    {
      tmp_counter_2 = (tmp_counter + 1) << 1;
      tmp_counter_3 = tmp_counter;
    }
    
    //redraw the column through buffer
    memset(ntbuf1, 0, sizeof(ntbuf1));
    memset(ntbuf2, 0, sizeof(ntbuf2));
    memset(attrbuf, 0, sizeof(attrbuf));
    //we start at 1 as we don't want to modify the ceiling
    for (j = 1; j < 13 ; ++j)
    {
      switch ((boards[current_player][tmp_counter][j]))
      {// HERE !!!!!!! tmp_counter ? manque + 6 pour p2
        case EMPTY:
          clear_metatile(j-1);
          attrbuf[j>>1] = return_tile_attribute_color(2,tmp_counter_2,j*2);
          break;
        case OJAMA:
          set_metatile(j-1,0xdc);
          attrbuf[j>>1] = return_tile_attribute_color(0,tmp_counter_2,j*2);
          break;
        case PUYO_RED:
          set_metatile(j-1,0xd8);
          attrbuf[j>>1] = return_tile_attribute_color(0,tmp_counter_2,j*2);
          break;
        case PUYO_BLUE:
          set_metatile(j-1,0xd8);
          attrbuf[j>>1] = return_tile_attribute_color(1,tmp_counter_2,j*2);
          break;
        case PUYO_GREEN:
          set_metatile(j-1,0xd8);
          attrbuf[j>>1] = return_tile_attribute_color(2,tmp_counter_2,j*2);
          break;
        case PUYO_YELLOW:
          set_metatile(j-1,0xd8);
          attrbuf[j>>1] = return_tile_attribute_color(3,tmp_counter_2,j*2);
          break;
      }
    } 
   
    //remplir les buffers nt et attr et ensuite faire le put !
    addr = NTADR_A(((tmp_counter_3)*2)+2, 2 );// le buffer contient toute la hauteur de notre tableau ! on commence en haut, donc 2
    vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 24);
    vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 24);
    put_attr_entries((nt2attraddr(addr)), 7);
    
  }
  else
  {
    /*if (current_player != 0)
    {
      tmp_counter_2 = tmp_counter + 6;
    }
    else
    {
      tmp_counter_2 = tmp_counter;
    }*/
    //if something "fall" the counter is always reset to its 0 to 5 equivalent
    //so if nothing fall and we reach 11 (5th column) then a full "loop" as been done and we can continue
    if (puyo_found == 0)
    {  
      column_height[current_player][tmp_counter] = 190;
    }
    else
    {
      //if puyo_found keep the height of the first puyo found, with no fall
      //this is the heighest in the stack.
      column_height[current_player][tmp_counter] = ((puyo_found-1)*16) - 2;
    }
    
    if (step_p_counter[current_player] == 11)
    {
      if (step_p[current_player] == FALL)
      {
        step_p[current_player] = CHECK_ALL;
        step_p_counter[current_player] = 255;
      }
      else
      {
        //FALL_OJAMA case, we go to show_next,
        step_p[current_player] = SHOW_NEXT;
        //we set 0 because the show_next with that counter at 1 will update ojama display list with manage_point
        step_p_counter[current_player] = 0;
        step_ojama_fall[current_player] = 0;
      }
    }
    
  }
  
  ++step_p_counter[current_player];
  return;
}

// Calculate the point and update the score plus the ojama on top of opponent board
// 2 steps: first score calculation plus display under player board
// 2nd: tile calculation based on ojamas[] and display
// doing both at the same time makes the screen jump...
void manage_point()
{
  //based on this formula: https://www.bayoen.fr/wiki/Tableau_des_dommages
  //dommage hit = (10*nb_puyos_destroyed)*(hit_power + color_bonus + group_bonus)
  char str[6];
  register word addr;
  byte tmp_mask = 0, i = 0, j = 0;
  //const byte tile_offset = (current_player == 0 ? 0 : 16);
  unsigned long int tmp_score = 0;
  
  if (step_p_counter[current_player] == 0/*(current_player == 0)  ? (step_p1_counter == 0):(step_p2_counter == 0)*/)
  {
    //hit power
    tmp_score = (nb_hit[current_player] <= 3) ? ((nb_hit[current_player]-1) << 3) : ((nb_hit[current_player]-3) << 5);

    //color_bonus
    //first get colors for current player, don't forget to shift it !
    tmp_mask = ((current_player == 0) ? mask_color_destroyed : mask_color_destroyed >> 4) & 0xf;
        
    //then get nb of colors used from the mask by bitshift, substract 1 and multiply by 3
    tmp_score += (((tmp_mask & 1) + ((tmp_mask & 2) >> 1) + ((tmp_mask & 4) >> 2) + ((tmp_mask & 8) >> 3)) - 1) * 3;


    // group_bonus
    if ( nb_group[current_player] > 0 )
    {
      tmp_score += ( (nb_group[current_player] < 7) ? (nb_group[current_player] + 1) : 10 );
    }

    //you need to raise the score if bonus are null, to avoid multiply by 0
    if (tmp_score == 0)
      tmp_score = 1;

    //Now the disappearing puyos
    tmp_score = tmp_score * ((unsigned long) nb_puyos_destroyed[current_player] * 10);

    score[current_player] += tmp_score;

    //WIP add the opponent ojama removal from current player stack !
    if (current_player == 0)
    {
      ojamas[2] += tmp_score;
      if (ojamas[0] > 0)
      {  
        ojamas[0] = (ojamas[0] - tmp_score > ojamas[0] ) ? 0 : ojamas[0] - tmp_score ;
      }
    }
    else
    {
      ojamas[0] += tmp_score;
      if (ojamas[2] > 0)
      {  
        ojamas[2] = (ojamas[2] - tmp_score > ojamas[2] ) ? 0 : ojamas[2] - tmp_score ;
      }
    }

    //TODO warikomi not handled yet
    sprintf(str,"Hit:%2d", nb_hit[current_player]);
    addr = NTADR_A(nt_x_offset[current_player],26);
    vrambuf_put(addr,str,6);
    sprintf(str,"%6lu", score[current_player]);
    addr = NTADR_A(6 + nt_x_offset[current_player],27);
    vrambuf_put(addr,str,6);
    
    //play hit sound
    play_hit(nb_hit[current_player]);
    
    //reinit value for next compute
    nb_puyos_destroyed[current_player] = 0;
    mask_color_destroyed = mask_color_destroyed & ((current_player == 0) ? 0xf0 : 0xf) /*0xF0*/;
    nb_group[current_player] = 0;
  }
  else
  {
    //display damages
    //note: at some point we will have to deal with too much things updated simultaneously
    memset(ntbuf1, 0, sizeof(ntbuf1));
    memset(ntbuf2, 0, sizeof(ntbuf2));
    //the number of ojama depends of the score
    //see https://www.bayoen.fr/wiki/Tableau_des_dommages
    //would be neater to put addresses into an enum...
    //1:    0xfc   ojama
    //6:    0xf8   bug ojama
    //30:   0xe4   rock
    //180:  0xe8   tilted rock
    //360:  0xec   star
    //720:  0xf0   crown
    //1440: 0xf4   comet
    
    //first let's get our score divided by 70
    tmp_score = ojamas[(current_player == 0 ? 2 : 0)] / 70;
    j = 0;
    //let's cheat, setup everything as ojamaless tile
    memset(str,0xc4, sizeof(str));
    
    for ( i = 0; i < 7  && j < 6 ; ++i)
    {
      //we go from higher score to lowest, checking the rest.
      if (tmp_score / damageList[i] > 0 )
      {
        //we use our previously declared str to store the address of the tile
        //we use byte_mask because as str it is there, avoidinf declaring something else
        //index_is reused too !
        for (tmp_mask = 0; tmp_mask < (tmp_score/damageList[i]) && j < 6 ; ++tmp_mask)
        {
          str[j] = damageTile[i];
          ++j;
        }   
      }
      tmp_score %= damageList[i];     
    }

    set_metatile(0,str[0]);
    addr = NTADR_A(20-nt_x_offset[current_player], 0);// le buffer contient toute la hauteur de notre tableau ! on commence en haut, donc 2
    //si je ne mets pas le VRAMBUF_VERT la tile n'est pas bien présentée...
    //ce qui oblige à faire 6 appels, il faudra que je me plonge dans cette histoire
    //plus profondément à un moment.
    vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
    vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);

    set_metatile(0,str[1]);
    addr = NTADR_A(22-nt_x_offset[current_player], 0);
    vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
    vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);

    set_metatile(0,str[2]);
    addr = NTADR_A(24-nt_x_offset[current_player], 0);
    vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
    vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);

    set_metatile(0,str[3]);
    addr = NTADR_A(26-nt_x_offset[current_player], 0);
    vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
    vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);

    set_metatile(0,str[4]);
    addr = NTADR_A(28-nt_x_offset[current_player], 0);
    vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
    vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);

    set_metatile(0,str[5]);
    addr = NTADR_A(30-nt_x_offset[current_player], 0);
    vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
    vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);

  }
}

//fall ojama damage on the player field
byte fall_ojama()
{
  /* Conditions :
  The other player must be in "PLAY" step when the step_counter of fall_ojama is at 0
  We can only fall 5 rows at a time, then it goes to SHOW_NEXT and player continue to play
  The cell [2][1] must be free, if not game over
  The ojama score should be superior to 0
  */
  byte tmp_counter = step_p_counter[current_player];
  byte opponent_status = step_p[~current_player & 1]; //~0 & 1 donne 1 et ~1 & 1 donne 0;
  byte fall = 0, i = 0, top_line_space = 0;
  
  //char str[32];

  if ((step_ojama_fall[current_player] == 0 && opponent_status != PLAY) )
  {
    //inutile de continuer on passe à SHOW_NEXT
    step_p[current_player] = SHOW_NEXT;
    step_p_counter[current_player] = 0;
    return 0; //as we return 0 the ojama won't fall
  }
  
  //we need to let the fall_board function fall every column,
  //something it will do in 6 frames
  //so we only add the new ojama to fall on step_p_counter%6 ==0
  if (step_p_counter[current_player]%6 != 0)
  {
    return 1;
  }
  
  //add a line max of ojama on one row 
  if ( (step_ojama_fall[current_player] < 5) && (ojamas[current_player<<1] >= 70))
  {
    if ( ojamas[current_player<<1] >= 420)
    {
      // over 70*6, 420, a full line can be added
      for (i = 0; i < 6; ++i)
      {
        if ( (boards[current_player][i][0]/* & mask >> shift*/) == EMPTY)
        {
          boards[current_player][i][0] = OJAMA;
          if ( (ojamas[current_player<<1] - 70) < ojamas[current_player<<1]) //if not inferior then less than 70 !
          { 
            ojamas[current_player<<1] -= 70;
          }
          else
          {
            ojamas[current_player<<1] = 0;
            break;//no points, no need to continue
          }
        }
      }
    }
    else
    {
      //less than a line, we have to randomize the popping
      //look for empty spots on the top hidden line:
      top_line_space = 0;
      for (i = 0; i < 6; ++i )
      {
        if ((boards[current_player][i][0]) == EMPTY)
        {
          ++top_line_space;
        } 
      }
      if (top_line_space > 0)
      {
        if (top_line_space > (ojamas[current_player<<1] / 70) )
        {
          i = 0;
          //more space than ojama, we randomize the fall
          while (ojamas[current_player<<1] >= 70)
          {
            if ((boards[current_player][i][0]) == EMPTY && (rand8() & 1))
            {
              //boards[i][0] |= (OJAMA << shift);
              boards[current_player][i][0] = OJAMA;
              ojamas[current_player<<1] -= 70;
            }
            ++i;
            i = i%6;//to loop
          }
        }
        else
        {
          //less space than ojama, we fill every holes, the remaining ojamas will fall at next step
          for (i = 0; i < 6; ++i )
          {
            if ((boards[current_player][i][0]) == EMPTY)
            {
              //boards[i][0] |= (OJAMA << shift);
              boards[current_player][i][0] = OJAMA;
              ojamas[current_player<<1] -= 70;
            } 
          }
        }
      }
      
    }
    step_ojama_fall[current_player]++;
  }
 
  return  1;
}

// flush loser screen into under the play field
void flush()
{
  byte tmp_counter_2, tmp_counter_3, j;
  register word addr;
  byte tmp_counter = step_p_counter[current_player]%6;

  if (current_player != 0)
  {
    tmp_counter_2 = (tmp_counter + 9) << 1;
    tmp_counter_3 = tmp_counter + 8;
  }
  else
  {
    tmp_counter_2 = (tmp_counter + 1) << 1;
    tmp_counter_3 = tmp_counter;
  }
  
   //tmp_boards contains the information we need
  //we start from the bottom, each cell will receive the one above itself
  tmp_boards[tmp_counter][14] = tmp_boards[tmp_counter][13];
  tmp_boards[tmp_counter][13] = tmp_boards[tmp_counter][12];
  tmp_boards[tmp_counter][12] = tmp_boards[tmp_counter][11];
  tmp_boards[tmp_counter][11] = tmp_boards[tmp_counter][10];
  tmp_boards[tmp_counter][10] = tmp_boards[tmp_counter][9];
  tmp_boards[tmp_counter][9] = tmp_boards[tmp_counter][8];
  tmp_boards[tmp_counter][8] = tmp_boards[tmp_counter][7];
  tmp_boards[tmp_counter][7] = tmp_boards[tmp_counter][6];
  tmp_boards[tmp_counter][6] = tmp_boards[tmp_counter][5];
  tmp_boards[tmp_counter][5] = tmp_boards[tmp_counter][4];
  tmp_boards[tmp_counter][4] = tmp_boards[tmp_counter][3];
  tmp_boards[tmp_counter][3] = tmp_boards[tmp_counter][2];
  tmp_boards[tmp_counter][2] = tmp_boards[tmp_counter][1];
  tmp_boards[tmp_counter][1] = tmp_boards[tmp_counter][0];
  tmp_boards[tmp_counter][0] = EMPTY;  //last one receive empty

  
  
  //redraw the column through buffer
  memset(ntbuf1, 0, sizeof(ntbuf1));
  memset(ntbuf2, 0, sizeof(ntbuf2));
  memset(attrbuf, 0, sizeof(attrbuf));
  //we start at 1 as we don't want to modify the ceiling
  for (j = 1; j < 15 ; ++j)
  {
    switch (tmp_boards[tmp_counter][j])
    {
      case EMPTY:
        clear_metatile(j-1);
        attrbuf[j>>1] = return_tile_attribute_color(2,tmp_counter_2,j*2);
        break;
      case OJAMA:
        set_metatile(j-1,0xdc);
        attrbuf[j>>1] = return_tile_attribute_color(0,tmp_counter_2,j*2);
        break;
      case PUYO_RED:
        set_metatile(j-1,0xd8);
        attrbuf[j>>1] = return_tile_attribute_color(0,tmp_counter_2,j*2);
        break;
      case PUYO_BLUE:
        set_metatile(j-1,0xd8);
        attrbuf[j>>1] = return_tile_attribute_color(1,tmp_counter_2,j*2);
        break;
      case PUYO_GREEN:
        set_metatile(j-1,0xd8);
        attrbuf[j>>1] = return_tile_attribute_color(2,tmp_counter_2,j*2);
        break;
      case PUYO_YELLOW:
        set_metatile(j-1,0xd8);
        attrbuf[j>>1] = return_tile_attribute_color(3,tmp_counter_2,j*2);
        break;
      case 255:
        set_metatile(j-1, 0xc4);
        attrbuf[j>>1] = return_tile_attribute_color(0,tmp_counter_2,j*2);
    }
  } 

  //remplir les buffers nt et attr et ensuite faire le put !
  addr = NTADR_A(((tmp_counter_3)*2)+2, 2 );// le buffer contient toute la hauteur de notre tableau ! on commence en haut, donc 2
  vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 28);
  vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 28);
  put_attr_entries((nt2attraddr(addr)), 7);
  
  return;
}

//update the color of the next pair to come between fields
void update_next()
{
  // current position is p1_puyo_list_index, 
  //We have to remember that one byte contains 4 colors/puyo (2 bits per color)
  //we move only by 2 however
  
  // puyo_list       p1_puyo_list_index
  // (p1_puyo_list_index>>1) retourne le bon index puisqu'on a 4 paires par index
  // ensuite on décale sur le bon élément de l'index 
  // 2 bits pour chaque puyo=> on décale à droite (0<<0, 1<<2, 2<<4,3<<6)
  // et on fait & 3 pour ne garder que les 2 premiers bits
  
  //puyoSeq[(puyo_list[(p1_puyo_list_index>>1)]>>((((p1_puyo_list_index%2)*2)+i)*2))&3]);

  //I still quite don't get how this tile buffering fuctions works
  //So I do it like..that, and it's ugly.
  
  memset(attrbuf, 0, sizeof(attrbuf));
  attrbuf[0] = return_tile_attribute_color((puyo_list[((p_puyo_list_index[current_player]+1)>>1)]>>(((((p_puyo_list_index[current_player]+1)%2)*2)+0)*2))&3,14+(current_player<<1),4); 
  put_attr_entries((nt2attraddr( NTADR_A(14+(current_player<<1), 4 ))), 1);
  attrbuf[0] = return_tile_attribute_color((puyo_list[((p_puyo_list_index[current_player]+1)>>1)]>>(((((p_puyo_list_index[current_player]+1)%2)*2)+1)*2))&3,14+(current_player<<1),6); 
  put_attr_entries((nt2attraddr( NTADR_A(14+(current_player<<1), 6 ))), 1);
  //attrbuf[0] = return_tile_attribute_color(0,14+(i<<1),8); 
  //put_attr_entries((nt2attraddr( NTADR_A(14+(i<<1), 8 ))), 1);
  attrbuf[0] = return_tile_attribute_color((puyo_list[((p_puyo_list_index[current_player]+2)>>1)]>>(((((p_puyo_list_index[current_player]+2)%2)*2)+0)*2))&3,14+(current_player<<1),10);
  put_attr_entries((nt2attraddr( NTADR_A(14+(current_player<<1), 10 ))), 1);
  attrbuf[0] = return_tile_attribute_color((puyo_list[((p_puyo_list_index[current_player]+2)>>1)]>>(((((p_puyo_list_index[current_player]+2)%2)*2)+1)*2))&3,14+(current_player<<1),12);
  put_attr_entries((nt2attraddr( NTADR_A(14+(current_player<<1), 12 ))), 1);; 
  return;
}


void build_field()
{
  //register word addr;
  //byte i, x, y;
  byte x, y;
  //Filling up boards with EMPTY
  for (x = 0; x < 6; ++x)
  {
    for (y = 0; y < 13; ++y)
    {
      boards[0][x][y] = EMPTY/* + (EMPTY << 4)*/;
      boards[1][x][y] = EMPTY/* + (EMPTY << 4)*/;
      tmp_boards[x][y] = 0;
    }
  }
  
  //initialize attribute table to 0;
  //0 color palette 0
  //85 color palette 1 (4 couleur par octets, 0b01010101)
  //170 color palette 2 0b10101010
  //255 color palette 3 0b11111111
  memset(attribute_table,255,sizeof(attribute_table));
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
    else if (x == 14 || x == 16) //14 et 16
    {/* il faudra ici mettre les puyo à venir !*/
      for (y = 0; y < PLAYROWS; y+=2)
      {
        if ( (y >= 4 && y <= 6) || (y >= 10 && y <= 12) )
        {
          vram_adr(NTADR_A(x,y));
          vram_put(0xc8);
          vram_put(0xca);
          vram_adr(NTADR_A(x,y+1));
          vram_put(0xc9);
          vram_put(0xcb);
        }
        else
        {
          vram_adr(NTADR_A(x,y));
          vram_put(0xc4);
          vram_put(0xc6);
          vram_adr(NTADR_A(x,y+1));
          vram_put(0xc5);
          vram_put(0xc7);
        }
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
      //les deux terrains de jeu
      for (y = 2; y < PLAYROWS - 4; y+=2)
      {
        vram_adr(NTADR_A(x,y));
        vram_put(0);
        vram_put(0);
        vram_adr(NTADR_A(x,y+1));
        vram_put(0);
        vram_put(0);
      }
    }
    
  }
  
  // copy attribute table from PRG ROM to VRAM
  vram_write(attribute_table, sizeof(attribute_table));
  
  //only draw menu if necessary
  if (step_p[0] == SETUP)
  {
    put_str(NTADR_C(11,10), "Puyo VNES");
    put_str(NTADR_C(4,15), "Border style  1  2  3  4");
    put_str(NTADR_C(4,17), "Border color  1  2  3  4");
    put_str(NTADR_C(4,19), "Music         O  1  2  3");
    put_str(NTADR_C(4,21), "Color Blind Mode  0  1");
    put_str(NTADR_C(6,24), "Press start to begin!");
  }
}

void init_round()
{
  byte i;
  // initialize actors
  for ( i = 0; i < 2; ++i)
  {
    actor_x[i][0] = start_pos_x[i]/*3*16 ou 11*16*/;
    actor_y[i][0] = start_pos_y[i][0]/*0*16*/;
    actor_dx[i][0] = 0;
    actor_dy[i][0] = 1;
    actor_x[i][1] = start_pos_x[i]/*3*16 ou 11*16*/;
    actor_y[i][1] = start_pos_y[i][1]/*1*16*/;
    actor_dx[i][1] = 0;
    actor_dy[i][1] = 1;
    p_puyo_list_index[i] = 0;
    
    previous_pad[i] = 0;
    input_delay_PAD_A[i] = 0;
    input_delay_PAD_B[i] = 0;
    input_delay_PAD_LEFT[i] = 0;
    input_delay_PAD_RIGHT[i] = 0;
    timer_grace_period[i] = GRACE_PERIOD; 
    counter_falling_back_up[i] = MAX_FALLING_BACK_UP;
    
    step_p[i] = PLAY;
    step_p_counter[i] = 0;
    current_player = i;
    update_next();
  }

  //setting column heights for both players
  for (i = 0; i < 6 ; ++i)
  {
    column_height[0][i] = 190;
    column_height[1][i] = 190;
  }

  return;
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

void handle_controler_and_sprites()
{
  pad = pad_poll(current_player);

  //update status of controller memory
  if (previous_pad[current_player]&PAD_LEFT && pad&PAD_LEFT)
    ++input_delay_PAD_LEFT[current_player];
  else
    input_delay_PAD_LEFT[current_player] = 0;
  if (previous_pad[current_player]&PAD_RIGHT && pad&PAD_RIGHT)
    ++input_delay_PAD_RIGHT[current_player];
  else
    input_delay_PAD_RIGHT[current_player] = 0;
  
  if (previous_pad[current_player]&PAD_A && pad&PAD_A)
    ++input_delay_PAD_A[current_player];
  else
    input_delay_PAD_A[current_player] = 0;
  if (previous_pad[current_player]&PAD_B && pad&PAD_B)
    ++input_delay_PAD_B[current_player];
  else
    input_delay_PAD_B[current_player] = 0;

  //you have to look at the leftmost or rightmost puyo
  //p1 puyo 0 & 1, p2 puyo 2 & 3
  if (actor_x[current_player][0] < actor_x[current_player][1])
  {
    //left/right
    if ( pad&PAD_LEFT && (actor_x[current_player][0] > (16+(current_player*128))) && (actor_y[current_player][0] <= column_height[current_player][(actor_x[current_player][0] >> 4) - pos_x_offset[current_player] - 1]) )
    {
      //add a bit of delay before going again to left
      if (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY)
      {
        /*actor_dx[current_player][0] = actor_x[current_player][0];
        actor_dx[current_player][1] = actor_x[current_player][1];*/
        actor_x[current_player][0] -= 16;
        actor_x[current_player][1] -= 16;
      }
    }
    else if ( pad&PAD_RIGHT && (actor_x[current_player][1] < (96+(current_player*128))) && (actor_y[current_player][1] <= column_height[current_player][(actor_x[current_player][1] >> 4) - pos_x_offset[current_player] + 1]) )
    {
      if (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY)
      {
        /*actor_dx[current_player][0] = actor_x[current_player][0];
        actor_dx[current_player][1] = actor_x[current_player][1];*/
        actor_x[current_player][0] += 16;
        actor_x[current_player][1] += 16;       
      }
    }
    else
    { //doing nothing
      /*actor_x[i*2] = 0;
          actor_x[(i*2)+1] = 0;*/
    }
    //buttons, the puyo rotating is always the one at the top
    //so with index at 0 (0 p1, 2 p2)
    if (pad&PAD_B && input_delay_PAD_B[current_player] == 0)
    { 
      //here as puyo[0] < puyo[1] we are at the left, if we press
      //B the puyo will go under the 2nd puyo
      //the delay has to be at 0, because we don't want it to turn automatically
      //you have to press each time        
      /*actor_dx[current_player][0] = actor_x[current_player][0];*/
      actor_y[current_player][0] += 16;
      actor_x[current_player][0] += 16;
    }
    if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
    { 
      //here as puyo[0] < puyo[1] we are at the left, if we press
      //A the puyo will go over the 2nd puyo
      /*actor_dx[current_player][0] = actor_x[current_player][0];*/
      actor_y[current_player][0] -= 16;
      actor_x[current_player][0] += 16;
    }   
  }
  else
  {

   // (actor_y[(i*2)+1] <= column_height[(actor_x[(i*2)+1] >> 4) - 1])

    if (actor_x[current_player][0] != actor_x[current_player][1])
    {
      //actor_x i is more to the right than actor_x i+1
      //going left or right
      if (pad&PAD_LEFT && (actor_x[current_player][1] > (16+current_player*128)) && (actor_y[current_player][1] <= column_height[current_player][(actor_x[current_player][1] >> 4) - pos_x_offset[current_player] - 1]) )
      {
        if (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY)
        {
         /* actor_dx[current_player][0] = actor_x[current_player][0];
          actor_dx[current_player][1] = actor_x[current_player][1];*/
          actor_x[current_player][0] -= 16;
          actor_x[current_player][1] -= 16;
        }
      }
      else if (pad&PAD_RIGHT && (actor_x[current_player][0] < (96+current_player*128)) && (actor_y[current_player][0] <= column_height[current_player][(actor_x[current_player][0] >> 4) - pos_x_offset[current_player] + 1]) )
      {
        if (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY)
        {
         /* actor_dx[current_player][0] = actor_x[current_player][0];
          actor_dx[current_player][1] = actor_x[current_player][1];*/
          actor_x[current_player][0] += 16;
          actor_x[current_player][1] += 16;   
        }
      }

      //puyo[0] > puyo[1], it's on its right
      if (pad&PAD_B && input_delay_PAD_B[current_player] == 0)
      { 
        //here as puyo[0] > puyo[1] we are at the right, if we press
        //A the puyo will go over the 2nd puyo
       /* actor_dx[current_player][0] = actor_x[current_player][0];*/
        actor_y[current_player][0] -= 16;
        actor_x[current_player][0] -= 16;
      }
      if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
      { 
        //here as puyo[0] > puyo[1] we are at the right, if we press
        //A the puyo will go under the 2nd puyo
        /*actor_dx[current_player][0] = actor_x[current_player][0];*/
        actor_y[current_player][0] += 16;
        actor_x[current_player][0] -= 16; 
      }   
    }
    else
    {
      //left or right movement with both actor on the same x
      if (pad&PAD_LEFT && (actor_x[current_player][0] > (16+current_player*128)) && (actor_y[current_player][0] <= column_height[current_player][(actor_x[current_player][0] >> 4) - pos_x_offset[current_player] - 1]) )
      {
        if (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY)
        {
          /*actor_dx[current_player][0] = actor_x[current_player][0];
          actor_dx[current_player][1] = actor_x[current_player][1];*/
          actor_x[current_player][0] -= 16;
          actor_x[current_player][1] -= 16;
        }
      }
      else if (pad&PAD_RIGHT && (actor_x[current_player][0] < (96+current_player*128)) && (actor_y[current_player][0] <= column_height[current_player][(actor_x[current_player][0] >> 4) - pos_x_offset[current_player] + 1]) )
      {
        if (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY)
        {
          /*actor_dx[current_player][0] = actor_x[current_player][0];
          actor_dx[current_player][1] = actor_x[current_player][1];*/
          actor_x[current_player][0] += 16;
          actor_x[current_player][1] += 16;
        }
      }
      
      //same x for both puyo
      //B we go on the left, A we go on the right
      if (pad&PAD_B && input_delay_PAD_B[current_player] == 0)
      { 
        //we need to know if puyo[0] is above or below puyo[1]
        // the lowest value is higher on the screen !
        if (actor_y[current_player][0] < actor_y[current_player][1])
        {
          //going from up to left
          ///are we on the side left side?
          if (actor_x[current_player][0] == (16+current_player*128))
          {
            //wall kick
            /*actor_dx[current_player][1] = actor_x[current_player][1];*/
            actor_x[current_player][1] += 16;
            actor_y[current_player][0] += 16;           
          }
          else
          {
            /*actor_dx[current_player][0] = actor_x[current_player][0];*/
            actor_x[current_player][0] -= 16;
            actor_y[current_player][0] += 16;
          }
        }
        else
        {  //going down to right
          if (actor_x[current_player][0] == (96+current_player*128))
          {
            //wall kick
            /*actor_dx[current_player][1] = actor_x[current_player][1];*/
            actor_x[current_player][1] -= 16;
            actor_y[current_player][0] -= 16;
          }
          else
          {
            /*actor_dx[current_player][0] = actor_x[current_player][0];*/
            actor_x[current_player][0] += 16;
            actor_y[current_player][0] -= 16; 
          }
        }
      }
      if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
      { 
        if (actor_y[current_player][0] < actor_y[current_player][1])
        {
          // going from up to right
          /*actor_dx[current_player][0] = actor_x[current_player][0];*/
          actor_x[current_player][0] += 16;
          actor_y[current_player][0] += 16;  
        }
        else
        {
          //going from down to left
          /*actor_dx[current_player][0] = actor_x[current_player][0];*/
          actor_x[current_player][0] -= 16;
          actor_y[current_player][0] -= 16; 
        }   
      } 
    }
  }
  //play rotation sound if button pressed
  if (pad&PAD_A || pad&PAD_B)
    play_rotation_sound();
  
  //test play bayoen_sample
  if (pad&PAD_START)
  {
    //step_p2 = FALL_OJAMA;
    /*step_p[1] = FALL_OJAMA;
    step_ojama_fall[1] = 0;
    step_p_counter[1] = 0;*/
    //debug
    play_flush();
    step_p[0] = FLUSH;
    step_p_counter[0] = 255;
    actor_dx[1][0] = -1;
  }
  
  previous_pad[current_player] = pad;
}

void main(void)
{
  char i,j;	// actor index
  char str[32];
  register word addr;

  setup_graphics();
  // draw message  
  /*vram_adr(NTADR_A(2,3));
  vram_write("HELLO BAYOEN", 12);*/
  build_field();
  generate_rng();

  //we start by waiting each player to be ready
  //the wait state also build the board and things like that 
  /*step_p[0] = WAIT;
  step_p[1] = WAIT;*/
  //No let's try starting by a menu
  step_p[0] = SETUP;
  step_p[1] = SETUP;
  step_p_counter[0] = 255;
  step_p_counter[1] = 255;
  //only for menu navigation
  menu_pos_x = 0;
  memset(menu_pos_y,0,sizeof(menu_pos_y));
  actor_x[0][0] = 135;
  actor_x[0][1] = 135;
  actor_x[1][0] = 135;
  actor_x[1][1] = 165;
  actor_y[0][0] = 104;
  actor_y[0][1] = 120;
  actor_y[1][0] = 136;
  actor_y[1][1] = 152;
  input_delay_PAD_LEFT[0] = 0; //to prevent multiple inputs
    
  blind_offset = 4;
  
  //init score and wins at 0
  memset(score,0,sizeof(score));
  memset(wins,0,sizeof(wins));
  memset(ready,0,sizeof(ready));

  //init sound & music
  apu_init();
  music_ptr = 0;
  // enable rendering
  ppu_on_all();
  //ppu_wait_frame();
  //scroll(0,240);
  // infinite loop
  while(1) {
    
    //set sound
    if (!music_ptr) start_music(music1);
    //next music 
    play_music();
    
    //get input
    oam_id = 0;

    if (oam_id!=0) 
      oam_hide_rest(oam_id);
    // ensure VRAM buffer is cleared
    ppu_wait_nmi();
    vrambuf_clear();
    
    //maybe put that in another while loop at the beginning to avoid a useless test
    //good for testing only here
    if (step_p[0] == SETUP)
    {
      if (step_p_counter[0] == 255)
      {
        scroll(0,255);//y==256 bottom screen,0 top 
        /*sprintf(str,"press start");
        addr = NTADR_C(10,15);
        vrambuf_put(addr,str,11);*/
        pad = pad_poll(0);
        if (pad&PAD_START)
          --step_p_counter[0];
        if (pad& PAD_DOWN && menu_pos_x < 3 && input_delay_PAD_LEFT[0] == 0)
        {  
           ++menu_pos_x;
           input_delay_PAD_LEFT[0] = 8;
        }
        if (pad& PAD_UP && menu_pos_x > 0 && input_delay_PAD_LEFT[0] == 0)
        {
          --menu_pos_x;
          input_delay_PAD_LEFT[0] = 8;
        }
        if (pad& PAD_RIGHT && menu_pos_y[menu_pos_x] < 3 && input_delay_PAD_LEFT[0] == 0)
        {
          ++menu_pos_y[menu_pos_x];
          actor_x[menu_pos_x/2][menu_pos_x%2] += 24;
          input_delay_PAD_LEFT[0] = 8;
        }
        if (pad& PAD_LEFT && menu_pos_y[menu_pos_x] > 0 && input_delay_PAD_LEFT[0] == 0)
        {
          --menu_pos_y[menu_pos_x];
          actor_x[menu_pos_x/2][menu_pos_x%2] -= 24;
          input_delay_PAD_LEFT[0] = 8;
        }
        //menu sprites
        // set sprite in OAM buffer, chrnum is tile, attr is attribute, sprid is offset in OAM in bytes
        // returns sprid+4, which is offset for a next sprite
        /*unsigned char __fastcall__ oam_spr(unsigned char x, unsigned char y,
					unsigned char chrnum, unsigned char attr,
					unsigned char sprid);*/
        oam_id = oam_spr(16, 104+16*menu_pos_x, 0xAE, 0, oam_id );
        oam_id = oam_spr(actor_x[0][0], actor_y[0][0], 0xAF, 0, oam_id);
        oam_id = oam_spr(actor_x[0][1], actor_y[0][1], 0xAF, 0, oam_id);
        oam_id = oam_spr(actor_x[1][0], actor_y[1][0], 0xAF, 0, oam_id);
        oam_id = oam_spr(actor_x[1][1], actor_y[1][1], 0xAF, 0, oam_id);
          
        if (input_delay_PAD_LEFT[0] > 0)
          --input_delay_PAD_LEFT[0];

      }
      else if (step_p_counter[0] == 0)
      {
        scroll(0,0);
        step_p[0] = WAIT;
        step_p[1] = WAIT;
        step_p_counter[0] = 0;
        step_p_counter[1] = 0;
      }
      else
      {
        scroll(0,step_p_counter[0]);
        --step_p_counter[0];
      }
      
      continue;
    }

    for (current_player = 0 ; current_player < 2 ; ++current_player)
    {

      if (step_p[current_player] == PLAY)
      {
        handle_controler_and_sprites();
        for (i = 0 ; i < 2 ; ++i)
        {
          // puyoseq[0] == red, 1 blue, 2  green, 3 yellow, the good one is taken from
          // puyo_list       p1_puyo_list_index
          // (p1_puyo_list_index>>1) retourne le bon index puisqu'on a 4 paires par index
          // ensuite on décale sur le bon élément de l'index 
          // 2 bits pour chaque puyo=> on décale à droite (0<<0, 1<<2, 2<<4,3<<6)
          // et on fait & 3 pour ne garder que les 2 premiers bits  
          //Debug => on bloque p2
          if (current_player == 0)
          {
            sprite_addr[current_player][i] = ((puyo_list[(p_puyo_list_index[current_player]>>1)]>>((((p_puyo_list_index[current_player]%2)*2)+i)*2))&3) + blind_offset;
            oam_id = oam_meta_spr(actor_x[current_player][i], actor_y[current_player][i], oam_id, puyoSeq[sprite_addr[current_player][i]]);
          }
          else
          {
            --actor_y[current_player][i];
            sprite_addr[current_player][i] = ((puyo_list[(p_puyo_list_index[current_player]>>1)]>>((((p_puyo_list_index[current_player]%2)*2)+i)*2))&3) + blind_offset;
            oam_id = oam_meta_spr(actor_x[current_player][i], actor_y[current_player][i], oam_id, puyoSeq[sprite_addr[current_player][i]]);
            //oam_id = oam_meta_spr(actor_x[current_player][i], actor_y[current_player][i], oam_id, puyoSeq[((puyo_list[(p_puyo_list_index[current_player]>>1)]>>((((p_puyo_list_index[current_player]%2)*2)+i)*2))&3) + blind_offset]);
          }
          if (actor_dy[current_player][i] != 0) 
            actor_y[current_player][i] += (actor_dy[current_player][i] + ((previous_pad[current_player]&PAD_DOWN)? 2 : 0));

          //test relative to column_height
          /*if (actor_dy[current_player][i] != 0 && column_height[current_player][(actor_x[current_player][i] >> 4) - pos_x_offset[current_player]] < actor_y[current_player][i])
          {
            //actor_dx indicates if the x of the puyo has changed, and the position where it was in pixel, to be converted in column nb
            if (actor_dx[current_player][i] != 0)
            {
              if ( column_height[current_player][(actor_dx[current_player][i] >> 4) - pos_x_offset[current_player]] < 190 )
                column_height[current_player][(actor_dx[current_player][i] >> 4) - pos_x_offset[current_player]] += 16; // the column height is lowered
              actor_dx[current_player][i] = 0;
            }
            actor_dy[current_player][i] = 0;        
            actor_y[current_player][i] = column_height[current_player][(actor_x[current_player][i] >> 4) - pos_x_offset[current_player]];
            column_height[current_player][(actor_x[current_player][i]>>4) - pos_x_offset[current_player]] -= 16;
          }*/
          //we will replace that by managing the only cases where column height may be different from "normal"
          //ie when puyos are on the same X but different y, and only when the puyo considered is on top of the other
          //it will give us an offset that we can apply later in our test
          column_height_offset = 0;
          if (actor_x[current_player][0] == actor_x[current_player][1])
          {
            if (i == 0)
            {
              if (actor_y[current_player][i] < actor_y[current_player][1])
                column_height_offset = -16;
            } 
            else
            {
              if (actor_y[current_player][i] < actor_y[current_player][0])
                column_height_offset = -16;
            }    
          }
          
          if (actor_dy[current_player][i] != 0 && (column_height[current_player][(actor_x[current_player][i] >> 4) - pos_x_offset[current_player]] + column_height_offset) < actor_y[current_player][i])
          {
            actor_dy[current_player][i] = 0; 
            actor_y[current_player][i] = column_height[current_player][(actor_x[current_player][i] >> 4) - pos_x_offset[current_player]] + column_height_offset;
            column_height[current_player][(actor_x[current_player][i]>>4) - pos_x_offset[current_player]] -= 16;
          }
          
        }

        if (timer_grace_period[current_player] < GRACE_PERIOD || (actor_dy[current_player][0] == 0 && actor_dy[current_player][1] == 0))
        {
          if (previous_pad[current_player]&PAD_DOWN)
            timer_grace_period[current_player] = 0;
          else
            --timer_grace_period[current_player];
         //commenté pas sûr que ce soit utile....
         // if (actor_x[current_player][1] == 0 && timer_grace_period[current_player] == 0)
           // column_height[current_player][(actor_x[current_player][1]>>4) - pos_x_offset[current_player]] = actor_y[current_player][1];
        }

      }
      else
      {
        //we need to move oam_id to not have an offset, should be a better way though...
        oam_id = oam_meta_spr(actor_x[current_player][0], actor_y[current_player][0], oam_id, puyoSeq[((puyo_list[(p_puyo_list_index[current_player]>>1)]>>((((p_puyo_list_index[current_player]%2)*2)+0)*2))&3) + blind_offset]);
        oam_id = oam_meta_spr(actor_x[current_player][1], actor_y[current_player][1], oam_id, puyoSeq[((puyo_list[(p_puyo_list_index[current_player]>>1)]>>((((p_puyo_list_index[current_player]%2)*2)+1)*2))&3) + blind_offset]);
      }

      //flush step, that's supposing one opponent has lost
      //we are unsing tmp_boards, which is slightly larger than boards table.
      //the two extra lines are for storing what will be display on the floor
      if (step_p[current_player] == FLUSH)
      {
        //memset(tmp_boards,0,sizeof(tmp_boards));
        //init step, we copy current boards into tmp_boards, and set the floor tiles too
        if (step_p_counter[current_player] == 255)
        {
          for ( i = 0; i < 6; ++i)
          {
            tmp_boards[i][0] = boards[current_player][i][0];
            tmp_boards[i][1] = boards[current_player][i][1];
            tmp_boards[i][2] = boards[current_player][i][2];
            tmp_boards[i][3] = boards[current_player][i][3];
            tmp_boards[i][4] = boards[current_player][i][4];
            tmp_boards[i][5] = boards[current_player][i][5];
            tmp_boards[i][6] = boards[current_player][i][6];
            tmp_boards[i][7] = boards[current_player][i][7];
            tmp_boards[i][8] = boards[current_player][i][8];
            tmp_boards[i][9] = boards[current_player][i][9];
            tmp_boards[i][10] = boards[current_player][i][10];
            tmp_boards[i][11] = boards[current_player][i][11];
            tmp_boards[i][12] = boards[current_player][i][12];
            tmp_boards[i][13] = 255; // we use 255 as a way to identify a floor tile
            tmp_boards[i][14] = 255;
          }
        }
        else
        {
          flush();
        }
        
        ++step_p_counter[current_player];
        
        if (step_p_counter[current_player] > 90) // 15 * 6 == 90
        {
          step_p[current_player] = WAIT;
          step_p_counter[current_player] = 0;
        }
        continue;
      }
      
      //wait for next round to start, each player must press A button
      if (step_p[current_player] == WAIT)
      {
        if (step_p[0] == step_p[1] && current_player == 0) //both are waiting
        {
          switch (step_p_counter[0])
          {
            case 0:
              //here reset the boards
              memset(boards, EMPTY, sizeof(boards));
              memset(tmp_boards, 0, sizeof(tmp_boards));
              //reset the score
              memset(score,0,sizeof(score));
              step_p_counter[0] = 1;
              break;
            case 1:
              //reset the graphics
              ppu_off();
              build_field();
              ppu_on_all();
              step_p_counter[0] = 2;
              break;
            case 2:
               //randomize new pairs
              //generate_rng(); 
              step_p_counter[0] = 3;
              break;
            case 3:
               //print score, hit counter, wins and message
              sprintf(str,"Hit:%2d", nb_hit[0]);
              addr = NTADR_A(/*2 + tile_offset*/2,26);
              vrambuf_put(addr,str,6);
              
              sprintf(str,"%6lu", score[0]);
              addr = NTADR_A(/*8 + tile_offset*/8,27);
              vrambuf_put(addr,str,6);
              
              sprintf(str,"%2d", wins[0]);
              addr = NTADR_A(14,26);
              vrambuf_put(addr,str,2);
              
              sprintf(str,"Press A");
              addr = NTADR_A(4,10);
              vrambuf_put(addr,str,7);
              step_p_counter[0] = 4;
              break;
            case 4:
              sprintf(str,"%2d", wins[1]);
              addr = NTADR_A(16,27);
              vrambuf_put(addr,str,2);
              
              sprintf(str,"Hit:%2d", nb_hit[0]);
              addr = NTADR_A(18,26);
              vrambuf_put(addr,str,6);
              
              sprintf(str,"%6lu", score[0]);
              addr = NTADR_A(24,27);
              vrambuf_put(addr,str,6);
   
              sprintf(str,"Press A");
              addr = NTADR_A(20,10);
              vrambuf_put(addr,str,7);
              step_p_counter[0] = 5;
              break;
            case 5:
              rand();// to force rng to change by waiting player to start
              //wait for player to press the start or A button and switch to PLAY state.
              for (i = 0; i< 2; ++i)
              {
                if (ready[i] != 1 )
                {
                  pad = pad_poll(i);
                  if (pad&PAD_A)
                  {
                    ready[i] = 1;
                    sprintf(str,"       ");
                    addr = NTADR_A((i==0)?4:20,10);
                    vrambuf_put(addr,str,7);
                  }
                }
              }
              if (ready[0] == 1 && ready[1] == 1)
              {
                step_p_counter[0] = 6;
                ready[0] = 0;
                ready[1] = 0;
              }
              break;
            case 6:
              generate_rng();
              step_p_counter[0] = 7;
              break;
            case 7:  
              init_round();
              continue;//we want to avoid the step_p_counter_increment
              break;
            default:
              break;
          }    
          //++step_p_counter[0];
        }
        continue;//no need to evaluate the other possibilities
      }
      
      // ojama fall, only when opponent is in step "PLAY" when counter  is at 0
      // only 5 rows at a time, which will translate to 5*6 columns in our case
      // only if ojama[x], the damage score, is superior to 0
      // and if second row, the first visible, and third column is empty (top row : 0, so [2][1])
      if (step_p[current_player] == FALL_OJAMA)
      {
        if (fall_ojama() == 1)
        {
          //make them fall a line, if fall board is not moving, then it will go to "CHECK_ALL" at the end of fall_board
          fall_board();
        }
        //step_p[current_player] = SHOW_NEXT;
        continue;
      }

      if (step_p[current_player] == FALL)
      {
        //execute before destroy to avoid doing destroy and fall consecutively
        fall_board();
        continue;
      }

      //update the next pair to come in the middle of the field
      if (step_p[current_player] == SHOW_NEXT)
      { 
        //if we came from fall_board after a fall_ojama, we have to update the ojama display list first
        //(a bit of a hack for show_next initial purpose...)
        if( step_p_counter[current_player] == 1) 
        {
          manage_point();
          step_p_counter[current_player] = 0;
        }
        else
        {
          //either the screen is filled and party is over, or we just continue
          if ( boards[current_player][2][1] == EMPTY)
          {
            update_next();
            step_p[current_player] = PLAY;
          }
          else
          {
            //end of the road...
            //that player lose !
            step_p[current_player] = FLUSH;
            step_p_counter[current_player] = 255;
            //hide sprites
            actor_x[0][0] = 254;
            actor_x[0][1] = 254;
            actor_x[1][0] = 254;
            actor_x[1][1] = 254;
            actor_y[0][0] = 254;
            actor_y[0][1] = 254;
            actor_y[1][0] = 254;
            actor_y[1][1] = 254;
            play_flush(); // play sound of flush
            if (current_player == 0)          
            { 
              actor_dx[0][0] = 1;// stop action of the other player
              actor_dx[0][1] = 1;
              actor_dy[0][0] = 0;
              actor_dy[0][1] = 0;
              step_p[1] = WAIT;
              ++wins[1];
            }
            else
            {
              actor_dx[1][0] = 1;// stop action of the other player
              actor_dx[1][1] = 1;
              actor_dy[1][0] = 0;
              actor_dy[1][1] = 0;
              step_p[0] = WAIT;
              ++wins[0];
            }
          }   
        }
        continue;
      }

      if (step_p[current_player] == POINT)
      {
        //executed before destroy to avoid doing destroy and fall consecutively
        if (step_p_counter[current_player] == 0)
          nb_hit[current_player] += 1;
        
        manage_point();
        ++step_p_counter[current_player];
        
        if (step_p_counter[current_player] == 2)
        {
          step_p[current_player] = FALL;
          step_p_counter[current_player] = 0;
        }
        continue;
      }

      if (step_p[current_player] == DESTROY)
      {
        //need to avoid to start check in the same loop
        //need to see if we need to subdivise the work in several pass
        destroy_board();
        continue;
      }

      if (step_p[current_player] == CHECK && step_p_counter[current_player] == 0)
      {
        //reinit variables for counting point
        nb_puyos_destroyed[current_player] = 0; //how many puyos are destroyed on that hit      
        //we keep only opponent maskcolor
        mask_color_destroyed =  (current_player == 0) ? mask_color_destroyed & 0xF0 : mask_color_destroyed & 0x0F; // LSB p1, MSB p2, bit mask at 1 for each color present in the hit. bit 0 red, bit 1 blue, bit 2 green, 3 yellow 
        nb_group[current_player] = 0;//if the group is over 4 puyos add the part over in this variable.

        //should_destroy = (check_board( ((actor_x[current_player][0]>>3) - 2) >> 1, ((actor_y[current_player][0]>>3)+1)>>1) > 0);
        should_destroy = (check_board( (actor_x[current_player][0]>>4) - pos_x_offset[current_player], ((actor_y[current_player][0]>>3)+1)>>1) > 0);
        //if both puyo had the same color it's useless to perform the second check....No because of splits !
        /*if ( (boards[current_player][((actor_x[current_player][1]>>3) - 2) >> 1][((actor_y[current_player][1]>>3)+1)>>1] & 8) != 8)
          should_destroy = (check_board( ((actor_x[current_player][1]>>3) - 2) >> 1, ((actor_y[current_player][1]>>3)+1)>>1) > 0) || should_destroy;*/
        if ( (boards[current_player][(actor_x[current_player][1]>>4) - pos_x_offset[current_player]][((actor_y[current_player][1]>>3)+1)>>1] & 8) != 8)
          should_destroy = (check_board( (actor_x[current_player][1]>>4) - pos_x_offset[current_player], ((actor_y[current_player][1]>>3)+1)>>1) > 0) || should_destroy;
        
        if (should_destroy)
        {
          step_p_counter[current_player] = 0;
          step_p[current_player] = DESTROY;
          //let's move sprites to not have them on screen when things explode
          //according to https://wiki.nesdev.com/w/index.php/PPU_OAM, we have to put them between EF and FF on Y and F9 anf FF on X
          actor_x[current_player][0] = 254;
          actor_y[current_player][0] = 254;
          actor_x[current_player][1] = 254;
          actor_y[current_player][1] = 254;
          should_destroy = 0;
        }
        else
        {
          actor_x[current_player][0] = start_pos_x[current_player]/*3*16*/;
          actor_y[current_player][0] = start_pos_y[current_player][0]/*0*/;
          actor_x[current_player][1] = start_pos_x[current_player]/*3*16*/;
          actor_y[current_player][1] = start_pos_y[current_player][1]/*16*/;
          actor_dy[current_player][0] = 1;
          actor_dy[current_player][1] = 1;
          ++p_puyo_list_index[current_player];
          /*
          step_p1 = SHOW_NEXT;*/
          step_p[current_player] = FALL_OJAMA;
          step_p_counter[current_player] = 0;
          // step_p1 = DESTROY;
          // Need to reset the boards flag to 0 after destroy!
        }
        continue;
      } else if (step_p[current_player] == CHECK && step_p_counter[current_player] != 0)
      {
        step_p_counter[current_player] = 0;
        continue;
      }

      //after fall (and so destroy) we need to recheck all the board
      //Everything is fallen at that point, so we can go from bottom
      //to top and stop searching as soon empty is found
      if (step_p[current_player] == CHECK_ALL)
      {
        if (step_p_counter[current_player] < 78)
        { //Start from the left column and go right, do bottom
          //1 column per step to keep some CPU (hopefully)
          /*i = 12;
          //must not be empty (5) and must not have the flag (8) set !
          while ( ((boards[step_p1_counter][i] & 7) != EMPTY) &&
                  ((boards[step_p1_counter][i] & 8) != 8) &&
                 i <= 12 )
          {
            should_destroy = (check_board(0, step_p1_counter, i) > 0) || should_destroy ;
            --i;
          }*/
          i = step_p_counter[current_player] / 13;
          j = 13 - (step_p_counter[current_player] % 13);
          if (((boards[current_player][i][j] & 7) != EMPTY) && ((boards[current_player][i][j] & 8) != 8))
            should_destroy = (check_board(/*0,*/ i, j) > 0) || should_destroy ;
          ++step_p_counter[current_player];

          i = step_p_counter[current_player] / 13;
          j = 13 - (step_p_counter[current_player] % 13);
          if (((boards[current_player][i][j] & 7) != EMPTY) && ((boards[current_player][i][j] & 8) != 8))
            should_destroy = (check_board(/*0,*/ i, j) > 0) || should_destroy ;
          ++step_p_counter[current_player];

          i = step_p_counter[current_player] / 13;
          j = 13 - (step_p_counter[current_player] % 13);
          if (((boards[current_player][i][j] & 7) != EMPTY) && ((boards[current_player][i][j] & 8) != 8))
            should_destroy = (check_board(/*0,*/ i, j) > 0) || should_destroy ;
          ++step_p_counter[current_player];
        }
        else
        {
          //test is over, let's destroy if necessary
          if (should_destroy)
          {
            step_p_counter[current_player] = 0;
            step_p[current_player] = DESTROY;
            should_destroy = 0;
          }
          else
          {
            actor_x[current_player][0] = start_pos_x[current_player]/*3*16*/;
            actor_y[current_player][0] = start_pos_y[current_player][0]/*0*/;
            actor_x[current_player][1] = start_pos_x[current_player]/*3*16*/;
            actor_y[current_player][1] = start_pos_y[current_player][1]/*16*/;
            actor_dy[current_player][0] = 1;
            actor_dy[current_player][1] = 1;
            ++p_puyo_list_index[current_player];
            //step_p1 = SHOW_NEXT;
            step_p[current_player] = FALL_OJAMA;
            step_p_counter[current_player] = 0;
            nb_hit[current_player] = 0;// hit combo counter
          }
        }
        continue;
      }

      if ( step_p[current_player] == PLAY && actor_dy[current_player][0] == 0 && actor_dy[current_player][1] == 0 && actor_dx[current_player][0] == 0 && actor_dx[current_player][1] == 0 && timer_grace_period[current_player] == 0 )
      {
        //vrambuf_clear();
        memset(ntbuf1, 0, sizeof(ntbuf1));
        memset(ntbuf2, 0, sizeof(ntbuf2));
        memset(attrbuf, 0, sizeof(attrbuf));
        /*column_height[(actor_x[0]>>4) - 1] -= 16;
        column_height[(actor_x[1]>>4) - 1] -= 16;*/

        set_metatile(0,0xd8);
        //test !
        //puyoSeq contient l'adresse des data du sprite, et l'adresse de la tile est à cette adresse +2
        //set_metatile(0,*(puyoSeq[sprite_addr[current_player][0]]+0x2));
          
        //set_attr_entry((((actor_x[0]/8)+32) & 63)/2,0,return_sprite_color(0));
        //attrbuf should take the color for 4 tiles !
        attrbuf[0] = return_tile_attribute_color(return_sprite_color(current_player << 1), actor_x[current_player][0]>>3,(actor_y[current_player][0]>>3)+1);
        //HACK for unknown reason attribute_table is not correctly updated if function return_attribute_color is called twice
        //like here
        //attribute_table[(((actor_y[0]>>3)+1)<<1) + ((actor_x[0]>>3)>>2)] = attrbuf[0];
        set_metatile(1,0xd8);
        /*sprintf(str,"table:%d %d %d %d",attrbuf[0],actor_x[0]>>3,(actor_y[0]>>3)+1,(((actor_y[0]>>3)+1)<<1) + ((actor_x[0]>>3)>>2));
        addr = NTADR_A(1,26);
        vrambuf_put(addr,str,20);*/

        //set_attr_entry((((actor_x[1]/8)+32) & 63)/2,1,return_sprite_color(1));
        attrbuf[1] = return_tile_attribute_color(return_sprite_color((current_player<<1) + 1), actor_x[current_player][1]>>3, (actor_y[current_player][1]>>3)+1);/*return_sprite_color(1) + return_sprite_color(1)<<2 + return_sprite_color(1) << 4 + return_sprite_color(1) << 6*/;
        /*sprintf(str,"table:%d %d %d %d",attrbuf[1],actor_x[1]>>3,(actor_y[1]>>3)+1,(((actor_y[1]>>3)+1)<<1) + ((actor_x[1]>>3)>>2));
        addr = NTADR_A(1,27);
        vrambuf_put(addr,str,20);*/

        addr = NTADR_A((actor_x[current_player][0]>>3), (actor_y[current_player][0]>>3)+1);
        vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
        vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
        vrambuf_put(nt2attraddr(addr), &attrbuf[0], 1);

        addr = NTADR_A((actor_x[current_player][1]>>3), (actor_y[current_player][1]>>3)+1);
        vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
        vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
        vrambuf_put(nt2attraddr(addr), &attrbuf[1], 1);

        //updating the board, if things are done correctly attrbuf contains the color to be used
        //Still need to convert coordinates ! And not overwrite the value for the opponent board !
        update_boards();
        step_p[current_player] = CHECK;
        play_puyo_fix(); //play sound of puyo fixing into position
        timer_grace_period[current_player] = GRACE_PERIOD; 
        
      }
    }
    
    /*if (oam_id!=0) 
      oam_hide_rest(oam_id);
    // ensure VRAM buffer is cleared
    ppu_wait_nmi();
    vrambuf_clear();*/
    
    //scroll(0,256);//y==256 bottom screen,0 top 
  }
}

//bach sonate pour violoncelle N°1
const byte music1[]= {
0x16,0x89,0x1d,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x1d,0x89,0x26,0x89,0x1d,0x89,0x16,0x89,0x1d,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x1d,0x89,0x26,0x89,0x1d,0x89,0x16,0x89,0x1f,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x1f,0x89,0x27,0x89,0x1f,0x89,0x16,0x89,0x1f,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x1f,0x89,0x27,0x89,0x1f,0x89,0x16,0x89,0x21,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x21,0x89,0x27,0x89,0x21,0x89,0x16,0x89,0x21,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x21,0x89,0x27,0x89,0x21,0x89,0x16,0x89,0x22,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x26,0x89,0x22,0x89,0x16,0x89,0x22,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x26,0x89,0x21,0x89,0x16,0x89,0x1f,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x1f,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x1a,0x89,0x1d,0x89,0x1c,0x89,0x1a,0x89,0x1c,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x1c,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x24,0x89,0x29,0x89,0x28,0x89,0x29,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1d,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x13,0x89,0x1a,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x1a,0x89,0x22,0x89,0x1a,0x89,0x13,0x89,0x1a,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x1a,0x89,0x22,0x89,0x1a,0x89,0x13,0x89,0x1c,0x89,0x1d,0x89,0x1f,0x89,0x1d,0x89,0x1c,0x89,0x1a,0x89,0x18,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x29,0x89,0x28,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x29,0x89,0x24,0x89,0x29,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x23,0x89,0x1d,0x89,0x20,0x89,0x1f,0x89,0x20,0x89,0x1d,0x89,0x23,0x89,0x1d,0x89,0x26,0x89,0x1d,0x89,0x20,0x89,0x1f,0x89,0x20,0x89,0x1d,0x89,0x23,0x89,0x1d,0x89,0x1b,0x89,0x1f,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x24,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1f,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x24,0x89,0x21,0x89,0x1f,0x89,0x1e,0x89,0x21,0x89,0x1e,0x89,0x21,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x21,0x89,0x1e,0x89,0x21,0x89,0x1e,0x89,0x21,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x21,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x24,0x89,0x21,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x18,0x89,0x16,0x89,0x15,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x15,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x16,0x89,0x1a,0x89,0x20,0x89,0x1f,0x89,0x20,0x89,0x1a,0x89,0x20,0x89,0x1a,0x89,0x16,0x89,0x1a,0x89,0x20,0x89,0x1f,0x89,0x20,0x89,0x1a,0x89,0x20,0x89,0x1a,0x89,0x16,0x89,0x1b,0x89,0x1f,0x89,0x1d,0x89,0x1f,0x89,0x1b,0x89,0x1f,0x89,0x1b,0x89,0x16,0x89,0x1b,0x89,0x1f,0x89,0x1d,0x89,0x1f,0x89,0x1b,0x89,0x1f,0x89,0x1b,0x89,0x16,0x89,0x21,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x21,0x89,0x27,0x89,0x21,0x89,0x16,0x89,0x21,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x21,0x89,0x27,0x89,0x21,0x89,0x16,0x89,0x1d,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x18,0x89,0x16,0x89,0x15,0x89,0x13,0x89,0x11,0x89,0x10,0x89,0x18,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x10,0x89,0x18,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x0f,0x89,0x18,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x0f,0x89,0x18,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x0f,0x89,0x18,0x89,0x1d,0x89,0x21,0x89,0x24,0x89,0x28,0x89,0x29,0x9b,0x18,0x89,0x1a,0x89,0x1b,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x24,0x89,0x21,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x24,0x89,0x21,0x89,0x22,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x29,0x89,0x2a,0x89,0x29,0x89,0x28,0x89,0x29,0x89,0x29,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x27,0x89,0x24,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x18,0x89,0x1a,0x89,0x1b,0x89,0x11,0x89,0x18,0x89,0x1d,0x89,0x21,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x16,0x89,0x18,0x89,0x1a,0x89,0x11,0x89,0x16,0x89,0x1a,0x89,0x1d,0x89,0x22,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x28,0x89,0x25,0x89,0x24,0x89,0x25,0x89,0x25,0x89,0x24,0x89,0x23,0x89,0x24,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x22,0x89,0x1f,0x89,0x1c,0x89,0x1a,0x89,0x18,0x89,0x1c,0x89,0x1f,0x89,0x22,0x89,0x24,0x89,0x28,0x89,0x29,0x89,0x28,0x89,0x29,0x89,0x24,0x89,0x21,0x89,0x1f,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x21,0x89,0x18,0x89,0x1d,0x89,0x1c,0x89,0x1a,0x89,0x18,0x89,0x16,0x89,0x15,0x89,0x13,0x89,0x11,0x92,0x27,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x27,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x18,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x27,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x27,0x89,0x24,0x89,0x29,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x27,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x27,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x1f,0x89,0x20,0x89,0x1d,0x89,0x21,0x89,0x1d,0x89,0x22,0x89,0x1d,0x89,0x23,0x89,0x1d,0x89,0x24,0x89,0x1d,0x89,0x25,0x89,0x1d,0x89,0x26,0x89,0x1d,0x89,0x27,0x89,0x1d,0x89,0x28,0x89,0x1d,0x89,0x29,0x89,0x1d,0x89,0x2a,0x89,0x1d,0x89,0x2b,0x89,0x1d,0x89,0x2c,0x89,0x1d,0x89,0x2d,0x89,0x1d,0x89,0x2e,0x89,0x26,0x89,0x1d,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x1d,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x2e,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x2d,0x89,0x27,0x89,0x1d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x1d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x16,0x26,0x2e,0xff
};
