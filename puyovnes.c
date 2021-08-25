//custom config file, explained there http://8bitworkshop.com/blog/docs/ide.md.html#8bitworkshopideusermanual/managingfiles/cc65customconfigfiles
//it's only purpose is to allocate/define/whatever room for the audio sample (by default DMC/SAMPLE size is too short)
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
#define DEBUG 0 // Currently P2 game is broken, need to check why.
/// GLOBAL VARIABLES
byte debug;
byte ia;
//note on CellType: PUYO_RED is first and not EMPTY for 0, because it's matching the attribute table
//(I think I will regret that decision later...)
//enum replaced by #define as enum are int, 16 bits and slower
//see https://github.com/ilmenit/CC65-Advanced-Optimizations#06---get-rid-of-enums---296-ticks-3-speedup
//typedef enum CellType {PUYO_RED, PUYO_BLUE, PUYO_GREEN, PUYO_YELLOW, OJAMA, EMPTY, PUYO_POP};
//Celltype
#define PUYO_RED 0
#define PUYO_BLUE 1
#define PUYO_GREEN 2
#define PUYO_YELLOW 3
#define OJAMA 4
#define EMPTY 5
#define PUYO_POP 6

/*typedef enum Step {SETUP, 	//0
                   PLAY, 	//1 
                   CHECK, 	//2
                   CHECK_ALL, 	//3
                   DESTROY, 	//4
                   FALL, 	//5
                   POINT, 	//6
                   SHOW_NEXT, 	//7
                   FALL_OJAMA, 	//8
                   FLUSH, 	//9
                   WAIT}; 	//A*/
//steps
#define SETUP 0
#define PLAY 1 	        //1 
#define CHECK 2 	//2
#define CHECK_ALL 3 	//3
#define DESTROY 4 	//4
#define FALL 5 	        //5
#define POINT 6 	//6
#define SHOW_NEXT 7 	//7
#define FALL_OJAMA 8 	//8
#define FLUSH 9 	//9
#define WAIT 0xA 	//A
#define UPDATE_HEIGHT 0xB  //B

/*byte seg_height;	// segment height in metatiles
byte seg_width;		// segment width in metatiles*/
byte seg_char;		// character to draw
byte seg_palette;	// attribute table value

byte current_player; // takes 0 or 1

byte x, y; //position in the board of the puyo to check;

//byte step_p1, step_p2;
//byte step_p1_counter, step_p2_counter;// indicates where we are in the process.
//we will use array for these new to easily switch from p1 to p2
byte step_p[2];
byte step_p_counter[2];
byte column_height[2][6];
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

//Pointers to addresses to speed-up access time
byte * board_address;
byte * tmp_boards_address;
byte * cell_address;
byte * tmp_cell_address;
byte * current_board_address;
byte * current_tmp_board_address;
byte * offset_address;
byte * current_actor_x;
byte * current_actor_y;
byte * current_column_height;
byte * current_displayed_pairs;
byte * base_address; // for update_boards
byte * current_current_damage_tiles; // for refresh_ojama_display


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
//DEF_METASPRITE_2x2(behind, 0x00, OAM_BEHIND);//yellow
//defined by hand to be sure to have the transparent first sprite in the assets
const unsigned char behind[]={\
        0,      0,      0,   OAM_BEHIND, \
        0,      8,      0,   OAM_BEHIND, \
        8,      0,      0,   OAM_BEHIND, \
        8,      8,      0,   OAM_BEHIND, \
        128};
//not needed
/*DEF_METASPRITE_2x2(ojama, 0xdc, 0);
DEF_METASPRITE_2x2(puyo_pop, 0xe0, 0);*/

//will list all our puyo to come, generated by RNG
byte puyo_list[PUYOLISTLENGTH];
byte p_puyo_list_index[2];
// this one is stocking in 01 the pair being played and in 23 and 46 the pair to display in next
// so basically the color or each puyo
byte displayed_pairs[2][6];

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
byte should_destroy[2]; //also for both players, or there is a risk that they remove the in current check_all result.
byte blind_offset; //offset to apply to get the correct sprite
byte sprite_addr[2][2]; //keep track of the addr of the sprite tile to convert from sprite to tile
byte current_color;
byte check_all_column_list[2]; //during CHECK_ALL, which columns must be checked, bit mask, column 0 : 1, 1:2, 2:4 ...column 5 : 32
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
/*char*//*byte column_height[2][6];*/ //char signed ? byte unsigned
char column_height_offset;
//constant for puyo physics
#define GRACE_PERIOD /*32*/ 64
#define MAX_FALLING_BACK_UP 8
byte timer_grace_period[2];
byte counter_falling_back_up[2];
#define FLAG 8
byte bg_tile;//address of top left, bottom left+1, top right+2, bottom right+4
byte bg_pal;//0 color palette 0, 85 color palette 1 (4 couleur par octets, 0b01010101), 170 color palette 2 0b10101010, 255 color palette 3 0b11111111
byte menu_pos_x;
byte menu_pos_y[4];
char str[32];
byte current_damage_tiles[2][6];
byte current_damage_tiles_index[2]; //the table indicate the player, the index is pointing at which current_damage_tiles we will fill.

//indexes only used in main
char i,j;	// actor index
//global indexes and variable to avoid declaring them each time
//gp=>general purpose, sorry I am bad at naming things
byte gp_i, gp_j, gp_k, tmp_counter, tmp_counter_2, tmp_counter_3, tmp_mask, tmp_attr, tmp_index, attr_x, attr_y, tmp_color;
/*register*/ word addr; //the compiler don't want to put that into register, will I lose some speed ?
  //const byte tile_offset = (current_player == 0 ? 0 : 16);
unsigned long int tmp_score[2], tmp_score2[2];
//variable for fall_board
byte fall, can_fall, previous_empty, puyo_found;
// indicates if the damage board needs o be refreshed. 0 = no refresh, 1 and above refresh needed or in progress
byte step_refresh_ojama_display;
// indicates if a soft_reset must be performed
byte soft_reset;

// Lookup tables to speed up the / and %  operation, there are filled at the end of the file
const byte div_6_lookup[];
const byte mod_6_lookup[]; //used in fall_board
//const byte div_12_look_up[]; no need for this one now, it's /6 and the result should be divided by >>1
const byte mod_12_lookup[];
const byte div_13_lookup[];

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
const byte* music_ptr = NULL;
const byte* music_selected_ptr = NULL;
#define SAMPLE_TEST 0xF800
//const byte bayoen[];
//extern const void * bayoen_sample_data[];

//end of music bloc

//le const machin const permet de placer l'info en rom et donc de gagner de la place en théorie
const unsigned char* const puyoSeq[9] = {
  puyo_red, puyo_blue, puyo_green, puyo_yellow, puyo_heart, puyo_rabbit_ghost, puyo_angry, puyo_yellow_bis, behind
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
  /*1024,512,256,128,32,8,1*/
};
/*  65535 / 1440 => 45, 0x2D
      *  65535 / 720  => 91, 0x5B
      *  65535 / 360  => 182, 0xB6
      *  65535 / 180  => 364, 0x16C
      *  65535 / 30   => 2184, 0x888
      *  65535 / 6    => 10922, 0x2AAA
      *  note: this can result in an erro*/
/*const unsigned int const damageListMult[7] =
{
  0x2D, 0x5B, 0xB6, 0x16C, 0x888, 0x2AAA, 0xFFFF
};*/

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
const byte const bg_tile_addr[4] = {0xc4,0x14,0xb0,0xb4};
const byte const floor_y = /*190*/192;
const byte next_columns_y[5] = {4,6,8,10,12};

//menu logo
//white points
const byte logo_whites[10][3]={
  {0x3F,0x7,0xF3},
  {0x40,0x87,0xF3},
  {0x9A,0x47,0xF3},
  {0xAD,0x47,0xF3},
  {0xA4,0xC7,0xF3},
  {0x9B,0x47,0xF3},
  {0x80,0x44,0x3},
  {0x80,0x44,0x3},
  {0x42,0x80,0x0},
  {0x3F,0x0,0x0}
};

//red points
const byte logo_reds[8]={
  0x7E,
  0xCB,
  0x81,
  0x80,
  0xC9,
  0xFF,
  0xFF,
  0x7E};

//declaration des fonctions, il parait que ça aide
word nt2attraddr(word a);
void set_metatile(byte y, byte ch);
void clear_metatile(byte y);
//void set_attr_entry(byte x, byte y, byte pal);
void put_attr_entries(word addr, byte length);
void generate_rng(void);
byte return_sprite_color(byte spr_index);
byte return_tile_attribute_color(byte color, byte spr_x, byte spr_y);
void update_boards(void); //return if a puyo has been placed on x = 2 and y = 1, then flush
byte check_board(void);
byte destroy_board(void);
void fall_board(void);
void manage_point(void);
void build_menu(void);
void build_field(void);
void setup_graphics(void);
void handle_controler_and_sprites(void);
void update_next(void);
//music and sfx related
byte next_music_byte(void);
void play_music(void);
void start_music(const byte* music);
void play_rotation_sound(void);
void play_hit(void); //pitch get higher with byte until reaching "bayoen !"
void play_puyo_fix(void); //when puyo is hitting ground, to be changed a bit tamed currently
void play_bayoen(void); // play bayoen sample
void play_flush(void); // flush sound when a player lose
byte fall_ojama(void); //fall ojama damage on the player field
void flush(void); // flush loser screen into under the play field
void init_round(void); //set actors, column_height and other things before a round
void put_str(unsigned int adr, const char *str);
void refresh_ojama_display(void); // refresh the ojama display above players playfield
void ia_move(void);// "IA" to avoid having the P2 lose too quickly
void main(void);

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

void play_hit()
{
  //APU_ENABLE(ENABLE_NOISE);
  //PULSE_CH0 is used by music, the sweep can be an issue
  //as it won't be deactivated automatically
  //so we use PULSE_CH1 for the moment
  if (nb_hit[current_player] < 8)
  {
    APU_PULSE_DECAY(PULSE_CH1, 2250-(nb_hit[current_player]*250), 192, 8, 1);
    //APU_PULSE_DECAY(PULSE_CH1, /*1121*/750+((hit-1)<<7), 192, 8, 1);
    APU_PULSE_SWEEP(PULSE_CH1,4,2,1);
    APU_NOISE_DECAY(0,8,3);
    //APU_PULSE_SWEEP_DISABLE(PULSE_CH0);
  }
  else
  {
    play_bayoen();
  } 
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
//not used !
/*void set_attr_entry(byte x, byte y, byte pal) {
  if (y&1) pal <<= 4;
  if (x&1) pal <<= 2;
  attrbuf[y/2] |= pal;
}*/

void put_attr_entries(word addr, byte length) {
  for (gp_k=0; gp_k<length; ++gp_k) {
    VRAMBUF_PUT(addr, attrbuf[gp_k], 0);
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
//note: the screen is disabled when rng is generated, so no need to use global variables here 
//as a slowdown won't be noticeable.
void generate_rng()
{
  //Let's use a table to simplify the loop code, [0] red, [1] : blue, [2], : green [3] : yellow
  byte nb_color[4] = {NBCOLORPOOL,NBCOLORPOOL,NBCOLORPOOL,NBCOLORPOOL};
  char tmp;
  byte i, j, redo;
  // puyo_list contains 64 (PUYOLISTLENGTH) char/8 bits info
  // each 8 bits contains 2 pairs as 1 puyo is a 2 bits color
  // as we have 4 colors palettes
  // the method used here is probably attrocious in term of optimization
  // but it should works.
  // we will be using rand8(), which is fast but...not very random =>nope using rand() finally
  rand();
  for (i = 0 ; i < PUYOLISTLENGTH ; ++i)
  {
    puyo_list[i] = 0; //reinit
    redo = 1; // loop until we get what we want;
    //get a number between 0 and 3
    for (j = 0 ; j < 4 ; ++j)
    {
      do
      {
        tmp = rand() & 3;
        if (nb_color[tmp] > 0)
        {
          --nb_color[tmp];
          redo = 0;
          puyo_list[i] += (tmp << (j<<1)); //j<<1 == j*2
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
  /*OAMSprite  *
  = (OAMSprite*)(0x200+16*spr_index);
  return (spr_ptr->attr & 0x3);*/
  //return (((OAMSprite*)(0x200+16*spr_index))->attr & 0x3);
  return (((OAMSprite*)(0x200+(spr_index<<4)))->attr & 0x3);
}

//based on sprite x/y position look for the bg attributes related to it
//then update the attributes with the color passes in parameter
byte return_tile_attribute_color(byte color, byte spr_x, byte spr_y)
{
  //byte attr; // now tmp_attr
  //byte index; // now tmp_index
  attr_x = spr_x&0xfc;
  attr_y = spr_y&0xfc;
  //byte mask = 3;
  tmp_mask = 3;
  //we must not override colors of the tiles around the one we change
  //We must determine were our meta sprite is located in the 4*4 metatile attributes
  //if x is odd it will be on the right, even left
  //if y is odd it will be on the bottom, even top
  //LSB is top left, MSB bottom right
  if (attr_y < spr_y)
  {
    tmp_mask <<= 4;
    color <<= 4;
  }
  if (attr_x < spr_x) 
  {
    tmp_mask <<= 2;
    color <<= 2;
  }
  // attribute position is y/2 + x/4 where y 2 LSB are 0
  tmp_index = (attr_y<<1) + (spr_x>>2);

  tmp_attr = attribute_table[tmp_index];
  //let's erase the previous attributes for the intended position
  tmp_attr &= ~tmp_mask; //~ bitwise not, so we keep only bit outside of mask from attr
  tmp_attr += color;
  attribute_table[tmp_index] = tmp_attr;
  return tmp_attr;
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
  base_address = board_address + (current_player? 0x4E:0);
  
  //we must not update the tile for sprite outside the board, so if y > 0xD0 (floor at 0xc0) and less than 0xF0 which is cell 0
  //so we compute first in gp_i the y of our sprite and test it
  gp_i = (((current_actor_y[0]>>3)+1)>>1); //the number of cells in the board to shift, if "under 0", between f0 and 0 the result is 15, 0xF that is in fact the cell 0
  if (gp_i == 0xF)
    gp_i =0;
  gp_j = nt_x_offset[current_player]; //Save the cycles ? save 8 bytes in all scenario
  if (gp_i < 0xD) //size of the table : 13, so from 0 to 12, 13 and 14 should not generate an update
  {
    //boards[current_player][((actor_x[current_player][0]>>3) - nt_x_offset[current_player]) >> 1][((actor_y[current_player][0]>>3)+1)>>1] = return_sprite_color(current_player<<1);
    cell_address = base_address + ((((current_actor_x[0]>>3) - gp_j) >> 1) * 0xD) + gp_i;
    *cell_address = return_sprite_color(current_player<<1);
  }

  gp_i = (((current_actor_y[1]>>3)+1)>>1);
   if (gp_i == 0xF)
    gp_i =0;
  if (gp_i < 0xD)
  {
    //boards[current_player][((actor_x[current_player][1]>>3) - nt_x_offset[current_player]) >> 1][((actor_y[current_player][1]>>3)+1)>>1] = return_sprite_color((current_player<<1)+1);
    cell_address = base_address + ((((current_actor_x[1]>>3) - gp_j) >> 1) * 0xD) + gp_i;
    *cell_address = return_sprite_color((current_player<<1)+1);
  }
  return;
}

// Look for puyo to destroy and flag them as such
//TO DO : vérifier si c'est vraiment plus rapide avec les tmp_boards en addressage direct...
//apparemment y'a certaines fois où c'est plus lent...
//le temps mini est plus court, mais le temps moyen est plus faible (de 2000 cycles)
//hypothèse : les *(machin +0xF) prennent plus de temps que prévu, comme indiqué là https://www.cc65.org/doc/coding.html
byte check_board(void)
{
  //static byte /*i, j, k,*/ current_color; //static are faster, but they are keeping there value outside of context
  /*byte counter = 0, tmp_counter = 0;*/ //counter => tmp_counter2, tmp_counter is a global variable now
  /*byte mask = 15, flag = 8, shift = 0;*/ 
  //byte destruction = 0;//tmp_coutner_3 !
  current_board_address = board_address + (current_player?0x4E:0);
  current_tmp_board_address = tmp_boards_address;
  tmp_counter = 0;
  tmp_counter_2 = 0; // counter
  tmp_counter_3 = 0; //destruction
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
  
  //Note : sizeof boards won't change, we could use a define here instead of computing it each time the function is called
  memset(tmp_boards,0,sizeof(tmp_boards));
  cell_address = current_board_address + (x*0xD) + y;
  //current_color = ((boards[current_player][x][y]));
  current_color = *cell_address;

  //OJAMA are not destroyed by being linked by 4 !
  //also if y == 0 then the puyo is hidden in the upper row and should not be checked !
  //0xc0 is the floor max, so anything above 0xD0 should not be considered too
  if (current_color == OJAMA || y == 0 || y >= 0xD0)
    return 0;

  //tmp_boards contains flag of the currently looked color
  tmp_cell_address = tmp_boards_address + (x*0xF) + y;
  //tmp_boards[x][y] = FLAG;
  *tmp_cell_address = FLAG;
  
  gp_i = (x - 1); //byte are unsigned, so -1 = 255, we will not enter in the while if i < 0
  cell_address = current_board_address + (gp_i*0xD) + y;
  tmp_cell_address = tmp_boards_address + (gp_i*0xF) + y;
  
  while ( gp_i < 6 )
  {
    if ( /*tmp_boards[gp_i][y]*/*tmp_cell_address != FLAG)
    {
      //if (current_color == ((boards[current_player][gp_i][y]) ))
      if (current_color == (*cell_address))
      {     
        //tmp_boards[gp_i][y] = FLAG;
        *tmp_cell_address = FLAG;
        ++tmp_counter_2;
      }
      else
      {  
       /* i = 7; //no need to continue if not found
        continue;*/
        break; //sort du while
      }
    }
    --gp_i;
    cell_address -= 0xD; //0xD d'écart dans le tableau entre chaque colonne.
    tmp_cell_address -= 0xF; //0xF d'écart dans le tableau temporaire entre chaque colonne.
  }
  
  gp_i = (x + 1);
  cell_address = current_board_address + (gp_i*0xD) + y;
  tmp_cell_address = tmp_boards_address + (gp_i*0xF) + y;
  
  while ( gp_i < 6 )
  {
    if ( /*tmp_boards[gp_i][y]*/ *tmp_cell_address != FLAG)
    {
      //if (current_color == ((boards[current_player][gp_i][y]) ))
      if (current_color == (*cell_address))
      {     
        //tmp_boards[gp_i][y] = FLAG;
        *tmp_cell_address = FLAG;
        ++tmp_counter_2;
      }
      else
      {
        /*i = 7; //no need to continue if not found
        continue;*/
        break;
      }
    }
    ++gp_i;
    cell_address += 0xD;
    tmp_cell_address += 0xF;
  }
  
  gp_i = (y - 1);
  cell_address = current_board_address + (x*0xD) + gp_i;
  tmp_cell_address = tmp_boards_address + (x*0xF) + gp_i;
  while ( gp_i > 0 )
  {
    if ( /*tmp_boards[x][gp_i]*/ *tmp_cell_address != FLAG)
    {
      //if (current_color == ((boards[current_player][x][gp_i]) ))
      if (current_color == (*cell_address))
      {     
        //tmp_boards[x][gp_i] = FLAG;
        *tmp_cell_address = FLAG;
        ++tmp_counter_2;
      }
      else
      {
        //i = 14; //no need to continue if not found
        break;
      }
    }
    --gp_i;
    --cell_address;
    --tmp_cell_address;
  }
  
  gp_i = (y + 1);
  cell_address = current_board_address + (x*0xD) + gp_i;
  tmp_cell_address = tmp_boards_address + (x*0xF) + gp_i;

  while ( gp_i < 13 )
  {
    if ( /*tmp_boards[x][gp_i]*/ *tmp_cell_address != FLAG)
    {
      //if (current_color == ((boards[current_player][x][gp_i]) ))
      if (current_color == (*cell_address))
      {     
        //tmp_boards[x][gp_i] = FLAG;
        *tmp_cell_address = FLAG;
        ++tmp_counter_2;
      }
      else
      {
        //i = 14; //no need to continue if not found
        break;
      }
    }
    ++gp_i;
    ++cell_address;
    ++tmp_cell_address;
  }
  //nothing found ? exit !
  if (tmp_counter_2 == 0)
    return 0;
  
  //ok so we got something, now looking for more
  gp_j = (x-1);
  
  //we go left, so we look above and below and right ?
  //we must do the column in both way (0 to 12 and 12 to 0) to avoid missing something
  cell_address = current_board_address + (gp_j * 0xD) + 12; //we start from the bottom, one column being from 0 (top) to 12 (bottom)
  tmp_cell_address = tmp_boards_address + (gp_j * 0x0F);
  offset_address = tmp_cell_address + 0x0F; //on regarde à droite car c'est là d'où on vient (étant donné qu'on va à gauche)

  while (gp_j < 6)
  { 
    //we start from the bottom and get up, we stop as we reach the ceiling or an empty cell
    //then we go to the bottom if we have found something on the way up, otherwise it is useless
    
    //if the column is empty, which is the case if the first cell at the bottom is, we can stop there
    if ((*cell_address) == EMPTY)
    {
      break;
    }
    
    //12, bottom
    if ( tmp_cell_address[12] != FLAG && (current_color == (*cell_address)) && 
        ( (offset_address[12] == FLAG) ||
          (tmp_cell_address[11] == FLAG) ))
    {
      tmp_cell_address[12] = FLAG;
      ++tmp_counter_2;
      ++tmp_counter;
    }
    --cell_address;
    tmp_counter_3 = tmp_cell_address[12]; //we wan't to check if a flag is met
    
    //11 to 2
    for (gp_i = 11; gp_i > 1 && (*cell_address) != EMPTY; --gp_i) //0 is in the ceiling and should not be tested, so we stop at 2 and process 1 independently
    {
      if ( tmp_cell_address[gp_i] != FLAG && (current_color == (*cell_address)) && 
          ( (offset_address[gp_i] == FLAG) ||
            (tmp_cell_address[gp_i-1] == FLAG) ||
            (tmp_cell_address[gp_i+1] == FLAG) ))
      {
        tmp_cell_address[gp_i] = FLAG;
        ++tmp_counter_2;
        ++tmp_counter;
      }
      --cell_address;
      tmp_counter_3 += tmp_cell_address[gp_i];
    }
    //if gp_i != 1 then we get out of the loop sooner, no need to check the top ceiling case
    //and we can start from there for the next loop
    if (gp_i == 1)
    {
      //1
      if (tmp_cell_address[1] != FLAG && (current_color == (*cell_address)) && 
          ( (offset_address[1] == FLAG) ||
            (tmp_cell_address[2] == FLAG) ))
      {
        tmp_cell_address[1] = FLAG;
        ++tmp_counter_2;
        ++tmp_counter;
      }
      tmp_counter_3 += tmp_cell_address[1];
      gp_k = 2;
    }
    else
    {  
      gp_k = gp_i+1;
      //++cell_address;
    }
        
    //if something has been found or added, we must go backwards to left nothing unchecked
    if (tmp_counter != 0)
    {
      //No need to redo the 1, 0 still hidden by ceiling
      ++cell_address; //should point on 2  or gp_k  
      
      //2 to 11
      for (gp_i = gp_k; gp_i < 12; ++gp_i)
      {
        if ( tmp_cell_address[gp_i] != FLAG && (current_color == (*cell_address)) && 
          ( (offset_address[gp_i] == FLAG) ||
            (tmp_cell_address[gp_i-1] == FLAG) ||
            (tmp_cell_address[gp_i+1] == FLAG) ))
        {
          tmp_cell_address[gp_i] = FLAG;
          ++tmp_counter_2;
          ++tmp_counter;
        }
        ++cell_address;
      }
      //12
      if ( tmp_cell_address[12] != FLAG && (current_color == (*cell_address)) && 
        ( (offset_address[12] == FLAG) ||
          (tmp_cell_address[11] == FLAG) ))
      {
        tmp_cell_address[12] = FLAG;
        ++tmp_counter_2;
        ++tmp_counter;
      }
    }
    
    if (tmp_counter_3 != 0)
    {
      tmp_counter = 0;
      tmp_counter_3 = 0;
      --gp_j;
      offset_address = tmp_cell_address;
      tmp_cell_address -= 0xF;
      cell_address = current_board_address + (gp_j * 0xD) + 12; // possible to do faster ?
    }
    else
    {
      //Nothing has been found for that column, so there is no chance something is found on the next, we can exit
      //gp_j = 7;
      break;
    }
  }
  
  gp_j = x+1;
  //we go right so we look left and above and below
  cell_address = current_board_address + (gp_j * 0xD) + 12; //we start from the bottom, one column being from 0 (top) to 12 (bottom)
  tmp_cell_address = tmp_boards_address + (gp_j * 0x0F);
  offset_address = tmp_cell_address - 0x0F; //on regarde à gauche car c'est là d'où on vient (étant donné qu'on va à droite)
  tmp_counter = 0; 
  tmp_counter_3 = 0;

  //TODO : the 2 loops, x-1 and x+1, are very similar now, can we combined them ?
  while (gp_j < 6)
  {
    //for not testing before or after the board
    //gp_k = gp_j-1; //useless as gp_j = x+1, and x can't be inferior to 0 ! So in this case offset_address is always in the table.
    
    //we start from the bottom and get up, we stop as we reach the ceiling or an empty cell
    //then we go to the bottom if we have found something on the way up, otherwise it is useless
    
     //if the column is empty, which is the case if the first cell at the bottom is, we can stop there
    if ((*cell_address) == EMPTY)
    {
      break;
    }
    
    //12, bottom
    if ( tmp_cell_address[12] != FLAG && (current_color == (*cell_address)) && 
        ( (offset_address[12] == FLAG) ||
          (tmp_cell_address[11] == FLAG) ))
    {
      tmp_cell_address[12] = FLAG;
      ++tmp_counter_2;
      ++tmp_counter;
    }
    --cell_address;
    tmp_counter_3 = tmp_cell_address[12];
    
    //11 to 2
    for (gp_i = 11; gp_i > 1 && (*cell_address) != EMPTY; --gp_i) //0 is in the ceiling and should not be tested, so we stop at 2 and process 1 independently
    {
      if ( tmp_cell_address[gp_i] != FLAG && (current_color == (*cell_address)) && 
          ( (offset_address[gp_i] == FLAG) ||
            (tmp_cell_address[gp_i-1] == FLAG) ||
            (tmp_cell_address[gp_i+1] == FLAG) ))
      {
        tmp_cell_address[gp_i] = FLAG;
        ++tmp_counter_2;
        ++tmp_counter;
      }
      --cell_address;
      tmp_counter_3 += tmp_cell_address[gp_i];
    }
    
    if (gp_i == 1)
    {
      //1
      if ( tmp_cell_address[1] != FLAG && (current_color == (*cell_address)) && 
          ( (offset_address[1] == FLAG) ||
           (tmp_cell_address[2] == FLAG) ))
      {
        tmp_cell_address[1] = FLAG;
        ++tmp_counter_2;
        ++tmp_counter;
      }
      tmp_counter_3 += tmp_cell_address[1];
      gp_k = 2;
    }
    else
    {
      gp_k = gp_i+1;
      //++cell_address;
    }
        
    //if something has been found or added, we must go backwards to left nothing unchecked
    if (tmp_counter != 0)
    {
      //No need to redo the 1, 0 still hidden by ceiling
      ++cell_address; //should point on 2   
      
      //2 to 11
      for (gp_i = gp_k; gp_i < 12; ++gp_i)
      {
        if ( tmp_cell_address[gp_i] != FLAG && (current_color == (*cell_address)) && 
          ( (offset_address[gp_i] == FLAG) ||
            (tmp_cell_address[gp_i-1] == FLAG) ||
            (tmp_cell_address[gp_i+1] == FLAG) ))
        {
          tmp_cell_address[gp_i] = FLAG;
          ++tmp_counter_2;
          ++tmp_counter;
        }
        ++cell_address;
      }
      //12
      if ( tmp_cell_address[12] != FLAG && (current_color == (*cell_address)) && 
        ( (offset_address[12] == FLAG) ||
          (tmp_cell_address[11] == FLAG) ))
      {
        tmp_cell_address[gp_i] = FLAG;
        ++tmp_counter_2;
        ++tmp_counter;
      }
    }
    
    if (tmp_counter_3 != 0)
    {
      //if tmp_counter_3 != 0 then the current column has something flag, even if not new from this loop
      //that justify the need to the check the next column
      tmp_counter = 0;
      tmp_counter_3 = 0;
      ++gp_j;
      offset_address = tmp_cell_address;
      tmp_cell_address += 0xF;
      cell_address = current_board_address + (gp_j * 0xD) + 12; // possible to do faster ?
    }
    else
    {
      //nothing found, so nothing to be found on the next column, we can exit
      // gp_j = 7;
      break;
    }
  }
  
  //we started from 0, so at 3 we have 4 to erase
  if (tmp_counter_2 >= 3)
  {
    //update the variable for point counting
    nb_puyos_destroyed[current_player] += (tmp_counter_2 + 1); //how many puyos are destroyed on that hit
    // LSB p1, MSB p2, bit mask at 1 for each color present in the hit. bit 0 red, bit 1 blue, bit 2 green, 3 yellow
    mask_color_destroyed |= (1 << shift[current_player]) << current_color;  
    nb_group[current_player] += (tmp_counter_2 + 1) - 4;//if the group is over 4 puyos add the part over in this variable.
    
    cell_address = current_board_address + 12;
    tmp_cell_address = current_tmp_board_address + 12;
    tmp_counter_3 = 0;
    //copy flag to boards
    for (gp_i = 0; gp_i < 6; ++gp_i)
    {
      current_board_address = cell_address;
      current_tmp_board_address = tmp_cell_address;
      for (gp_j = 12 ; gp_j <= 12 ; --gp_j)
      {
        if ( /*tmp_boards[gp_i][gp_j]*/ *tmp_cell_address == FLAG)
        {
          //boards[current_player][gp_i][gp_j] |= FLAG;
          *cell_address |= FLAG;
          ++tmp_counter_3;
          //quick hack for ojamas: we look around the current element, up done left right, if one is ojama, it's flagged too be destroyed too
          //note : it will probably slow down things a lot do do that :-s
          //look left
          /*if (gp_i>0 && boards[current_player][gp_i-1][gp_j] == OJAMA)
            boards[current_player][gp_i-1][gp_j] |= FLAG;*/
          if (gp_i>0 && *(cell_address - 0xD) == OJAMA)
            *(cell_address - 0xD) |= FLAG;
          //look right
          /*if (gp_i<5 && boards[current_player][gp_i+1][gp_j] == OJAMA)
            boards[current_player][gp_i+1][gp_j] |= FLAG*/
          if (gp_i<5 && *(cell_address + 0xD) == OJAMA)
            *(cell_address + 0xD) |= FLAG;
          //look up
          /*if (gp_j>0 && boards[current_player][gp_i][gp_j-1] == OJAMA)
            boards[current_player][gp_i][gp_j-1] |= FLAG;*/
          if (gp_j>0 && *(cell_address - 1) == OJAMA)
            *(cell_address - 1) |= FLAG;
          //look down
          /*if (gp_j<12 && boards[current_player][gp_i][gp_j+1] == OJAMA)
            boards[current_player][gp_i][gp_j+1] |= FLAG;*/
          if (gp_j<12 && *(cell_address + 1) == OJAMA)
            *(cell_address + 1) |= FLAG;
        }
        --cell_address;
        --tmp_cell_address;
      }
      cell_address = current_board_address + 0xD;
      tmp_cell_address = current_tmp_board_address + 0xF;
    }
  }
  return tmp_counter_3;
}

//puyo visual destroying after check_board
byte destroy_board()
{  
  tmp_counter = step_p_counter[current_player];
  
  if (tmp_counter < 6 || tmp_counter >= 12)
  {
    memset(ntbuf1, 0, sizeof(ntbuf1));
    memset(ntbuf2, 0, sizeof(ntbuf2));
    //memset(attrbuf, 0, sizeof(attrbuf)); // no need to reset the attributes, they are not changed !
    if (tmp_counter < 6)
    {
      set_metatile(0,0xe0); //0xe0 == puyo_pop   //step 0 we change the puyo to puyo_pop => e0
      // boards[current_player][tmp_counter][gp_j] = PUYO_POP + FLAG; // we don't want to lose the flag!
      //gp_k used to temporarily store the new board status
      gp_k = PUYO_POP + FLAG;
    }
    else
    {
      clear_metatile(0);   //step 1 we change the puyo_pop to nothing
      //boards[current_player][gp_i][gp_j] = EMPTY;
      gp_k = EMPTY;
    }
    
    //gp_i = tmp_counter%12;
    // a lookup table is a bit overkill for something only going up to 17..
    gp_i = mod_12_lookup[tmp_counter]; 
    cell_address = board_address + (current_player?0x4E:0) + gp_i*0xD;
    //and no need to recompute the x at each iteration !
    gp_i = (gp_i<<1) + nt_x_offset[current_player];
    for (gp_j = 0; gp_j < 13 ; ++gp_j)
    {
      if ((/*boards[current_player][gp_i][gp_j]*/ *cell_address & FLAG) > 0)
      {
        addr = NTADR_A(/*((gp_i)<<1) + nt_x_offset[current_player]*/gp_i, gp_j << 1 );
        vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
        vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
    
        //boards[current_player][gp_i][gp_j] = gp_k;
        *cell_address = gp_k;
      }
      ++cell_address;
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
  //byte j, j2; replaced by  gp_i and gp_j
  //register word addr;
  //byte can_fall = 0, previous_empty = 0, puyo_found = 0;
  //byte smask = 7;
  //byte attr_x_shift = 1;
  //byte fall = 0;
  //byte * cell_address;
  can_fall = 0;
  previous_empty = 0;
  puyo_found = 0;
  fall= 0;
  gp_k = 0; // the actual number of puyo found
  //byte tmp_counter = 0, tmp_counter_2 = 0, tmp_counter_3 = 0;
 
  //tmp_counter = step_p_counter[current_player]%6; /*step_p1_counter%6;*/ //prend 500cycles environ ! un if > 6 else serait-il mieux ?
  tmp_counter = mod_6_lookup[step_p_counter[current_player]];
  //on remplace par la techique A%B = A - B * (A/B) ? comme on ne fait pas la div c'est peut-être moins interessant
  //une méthode est indiquée là : http://homepage.cs.uiowa.edu/~jones/bcd/mod.shtml mais pas sûr de l'intéret
  offset_address = board_address + (current_player*0x4E) + tmp_counter*0xD;
  for (gp_j = 12 ; gp_j < 255; --gp_j)
  {
    //on va de bas en haut, par contre il faut insérer les valeurs dans ntbuff de haut en bas!
    //juste une itération fait 2200cycles :O 

    // ce test avec &s prend...700cycles, OMG ! et donc le reste en dessous 1500!
    // utiliser un || EMPTY+FLAG prend le même temps...
    //la version précédente a plus de can_fall !=1, donc teste moins avec le &, peut-être la raison de sa vitesse.
    //prend aussi 700 si on enlève le & 7, donc en gros, le simple test prend 700, ou l'accès à la mémoire. Voilà...
    //idem en bas chaque accès à boards[current_player][tmp_counter][gp_j] demande 700 cycles ?!? Why ?
    //gp_i = boards[current_player][tmp_counter][gp_j]; // ça prend 700cycles, mais pourquoi ????
    //so we compute the address by hand, much faster
    //basically, boards[0][0][0] is at board_address, select the player by moving of 0x4E == 78 case, a 13*6 boards
    // every columne are separated by 0xD (13) and every line by just one
    cell_address = (offset_address + gp_j); //player index missing there !
    gp_i = *cell_address;
    //peut-être qu'on peut calculer l'adresse suivante à la main ?
    if ((gp_i & 7) == EMPTY) 
    {
      can_fall = 1;
    }
    //if (((boards[current_player][tmp_counter][gp_j] & /*smask*/7)) != EMPTY)
    else
    {
      puyo_found = gp_j; // on est obligé de continuer à checker si empty à cause de ça, il nous faut la hauteur du puyo le plus haut
      ++gp_k;
      //c'est pour ça que l'ancienne version, avec la double boucle, était en fait plus rapide, elle ne retestait pas après
      //avoir trouvé le premier puyo qui était forcément le plus haut.
      if (!fall && can_fall)
        fall=1;
    }
      
    if (can_fall)
    {
      //on peut tomber et on a un truc pas vide =>on tombe :)
      if ( gp_j != 0 )
        //boards[current_player][tmp_counter][gp_j] = boards[current_player][tmp_counter][gp_j-1];
        *cell_address = *(cell_address-1);
      else    
        //boards[current_player][tmp_counter][gp_j] = EMPTY;
       *cell_address = EMPTY;
    }
      
    //problème, même si ça ne tombe pas on modifie les valeurs
    //et ça affiche n'importe quoi !
    /*switch ((boards[current_player][tmp_counter][gp_j]))
    {// HERE !!!!!!! tmp_counter ? manque + 6 pour p2
      case EMPTY:
        clear_metatile(12-gp_j);
        attrbuf[(12-gp_j)>>1] = return_tile_attribute_color(2,tmp_counter_2,(12-gp_j)*2);
        break;
      case OJAMA:
        set_metatile(12-gp_j,0xdc);
        attrbuf[(12-gp_j)>>1] = return_tile_attribute_color(0,tmp_counter_2,(12-gp_j)*2);
        break;//          
      default:
        set_metatile(12-gp_j,*(puyoSeq[boards[current_player][tmp_counter][gp_j]+blind_offset]+0x2));
        attrbuf[(12-gp_j)>>1] = return_tile_attribute_color(boards[current_player][tmp_counter][gp_j],tmp_counter_2,(12-gp_j)*2);
        break;
    }*/
  }
  //ce for prend 10000 cycles !?!?
  // il y a peut-être une solution pour remplir le ntbuf et attribuf en même temps
  //tout en évitant la deuxième boucle du can_fall
  /*for (gp_j = 0 ; gp_j < 13 ; ++gp_j)
  {
    if (can_fall != 1 && ( (boards[current_player][tmp_counter][gp_j] & 7)) != EMPTY)
    {
      puyo_found = gp_j;// if no puyo are found then the column is empty=> need to reset height
      //as long as no puyo is found, there is nothing to get down
      can_fall = 1;
      if (gp_j < 12)
        ++gp_j;  
    }

    if (can_fall == 1 && ( (boards[current_player][tmp_counter][gp_j] & 7)) == EMPTY)
    {
      //this is where things get interesting, lets move everything down.
      //we start from j and get up to avoid overwriting values
      for (gp_i = gp_j ; gp_i >= previous_empty && gp_i < 255 ; --gp_i)
      {
        if ( gp_i == previous_empty) 
          boards[current_player][tmp_counter][gp_i] = EMPTY; 
        else
          boards[current_player][tmp_counter][gp_i] = boards[current_player][tmp_counter][gp_i-1]; 
      }
      fall = 1;

      //careful we wan't to only fall of 1 puyo height per cycle !
      //So we keep the position of the last element that has falled so top there
      previous_empty = gp_j+1;
      can_fall = 0;
    }
  }*/
 
  if (fall == 1) //22.5k cycles à lui seul, donc a priori ça rame...//en mode adressage [][][] il prenait...28k environ
  {
    //this is falling, so we keep that column value to check it during CHECK_ALL step
    check_all_column_list[current_player] = check_all_column_list[current_player] | (1 << tmp_counter);
    //If we got a fall we reset the counter, then ...?
    //if (step_p[current_player] != FALL_OJAMA)
    step_p_counter[current_player] = tmp_counter;
    //As it fall the height of the column must be lowered:
    //utile ? sera recalculé plus tard normalement quand fall == 0
    /*if (current_column_height[tmp_counter] < floor_y) //let's avoid going under the floor
      current_column_height[tmp_counter] +=16;*/
    
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
    for (gp_j = 1; gp_j < 13 ; ++gp_j)
    {
      cell_address = (offset_address + gp_j); //player index missing there !
      gp_i = *cell_address;
      switch (/*(boards[current_player][tmp_counter][gp_j])*/ gp_i)
      {// HERE !!!!!!! tmp_counter ? manque + 6 pour p2
        case EMPTY:
          clear_metatile(gp_j-1);
          attrbuf[gp_j>>1] = return_tile_attribute_color(2,tmp_counter_2,gp_j<<1);
          break;
        case OJAMA:
          set_metatile(gp_j-1,0xdc);
          attrbuf[gp_j>>1] = return_tile_attribute_color(0,tmp_counter_2,gp_j<<1);
          break;//          
        default:
          set_metatile(gp_j-1,*(puyoSeq[/*boards[current_player][tmp_counter][gp_j]*/gp_i+blind_offset]+0x2));
          attrbuf[gp_j>>1] = return_tile_attribute_color(/*boards[current_player][tmp_counter][gp_j]*/gp_i,tmp_counter_2,gp_j<<1);
          break;
      }
    } 
   
    //remplir les buffers nt et attr et ensuite faire le put !
    addr = NTADR_A(((tmp_counter_3)<<1)+2, 2 );// le buffer contient toute la hauteur de notre tableau ! on commence en haut, donc 2
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
      if (gp_k == 0) // if gp_k still at 0 then there is no puyo in that column, otherwise it is full even on hidden row
        current_column_height[tmp_counter] = floor_y;
      else
        current_column_height[tmp_counter] = 0xf0;
    }
    else
    {
      //if puyo_found keep the height of the first puyo found, with no fall
      //this is the heighest in the stack.
      current_column_height[tmp_counter] = ((puyo_found-1)/**16*/<< 4) /*- 2*/;
    }
    
    if (step_p_counter[current_player] == 11)
    {
      if (step_p[current_player] == FALL)
      {
        if (boards[current_player][0][12] == EMPTY &&
            boards[current_player][1][12] == EMPTY &&
            boards[current_player][2][12] == EMPTY &&
            boards[current_player][3][12] == EMPTY &&
            boards[current_player][4][12] == EMPTY &&
            boards[current_player][5][12] == EMPTY )
        { 
          //all clear !
          score[current_player] += 2100;

          //WIP add the opponent ojama removal from current player stack !
          if (current_player == 0)
          {
            ojamas[2] += 2100;
            if (ojamas[0] > 0)
            {  
              ojamas[0] = (ojamas[0] - 2100 > ojamas[0] ) ? 0 : ojamas[0] - 2100 ;
            }
          }
          else
          {
            ojamas[0] += 2100;
            if (ojamas[2] > 0)
            {  
              ojamas[2] = (ojamas[2] - 2100 > ojamas[2] ) ? 0 : ojamas[2] - 2100 ;
            }
          }

          //TODO warikomi not handled yet
          sprintf(str,"Hit:AC");
          addr = NTADR_A(nt_x_offset[current_player],26);
          vrambuf_put(addr,str,6);
          sprintf(str,"%6lu", score[current_player]);
          addr = NTADR_A(6 + nt_x_offset[current_player],27);
          vrambuf_put(addr,str,6);

          //play hit sound
          play_bayoen();
          //temporary solution, as it takes too much time there ! maybe 60000 cycles for point counting alone...
          //Maybe adding a AC step, if size of rom allows it... 
          /*for ( step_p_counter[current_player] = 2; step_p_counter[current_player] < 11;  step_p_counter[current_player] ++)
          {
            manage_point();
          }*/
          step_refresh_ojama_display = 1;
          /*step_p_counter[current_player] = 2;
          manage_point();
          step_p_counter[current_player] = 3;
          manage_point();*/
        }
        step_p[current_player] = CHECK_ALL;
        step_p_counter[current_player] = 255;
      }
      else
      {
        //FALL_OJAMA case, we go to show_next,
        step_p[current_player] = SHOW_NEXT;
        step_refresh_ojama_display = 1;
        step_ojama_fall[current_player] = 0;
        step_p_counter[current_player] = 0;
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
  //const byte tile_offset = (current_player == 0 ? 0 : 16);
  //4 steps:
  //0 : calculate the score
  //1 : update hit and score, play sound
  //2 : compute the damage over opponent board
  //3 : update the tiles based on 2
   
  switch (step_p_counter[current_player])
  {
    case 0: // Compute the new score and ojamas
      //hit power
      tmp_score[current_player] = (nb_hit[current_player] <= 3) ? ((nb_hit[current_player]-1) << 3) : ((nb_hit[current_player]-3) << 5);

      //color_bonus
      //first get colors for current player, don't forget to shift it !
      tmp_mask = ((current_player == 0) ? mask_color_destroyed : mask_color_destroyed >> 4) & 0xf;

      //then get nb of colors used from the mask by bitshift, substract 1 and multiply by 3
      tmp_score[current_player] += (((tmp_mask & 1) + ((tmp_mask & 2) >> 1) + ((tmp_mask & 4) >> 2) + ((tmp_mask & 8) >> 3)) - 1) * 3;


      // group_bonus
      if ( nb_group[current_player] > 0 )
      {
        tmp_score[current_player] += ( (nb_group[current_player] < 7) ? (nb_group[current_player] + 1) : 10 );
      }

      //you need to raise the score if bonus are null, to avoid multiply by 0
      if (tmp_score[current_player] == 0)
        tmp_score[current_player] = 1;

      //Now the disappearing puyos
      tmp_score[current_player] = tmp_score[current_player] * ((unsigned long) nb_puyos_destroyed[current_player] * 10);

      score[current_player] += tmp_score[current_player];

      //WIP add the opponent ojama removal from current player stack !
      if (current_player == 0)
      {
        ojamas[2] += tmp_score[current_player];
        if (ojamas[0] > 0)
        {  
          ojamas[0] = (ojamas[0] - tmp_score[current_player] > ojamas[0] ) ? 0 : ojamas[0] - tmp_score[current_player] ;
        }
      }
      else
      {
        ojamas[0] += tmp_score[current_player];
        if (ojamas[2] > 0)
        {  
          ojamas[2] = (ojamas[2] - tmp_score[current_player] > ojamas[2] ) ? 0 : ojamas[2] - tmp_score[current_player] ;
        }
      }
      break;
    case 1: // refresh display of hit and score, play sound
      //TODO warikomi not handled yet
      sprintf(str,"Hit:%2d", nb_hit[current_player]);
      addr = NTADR_A(nt_x_offset[current_player],26);
      vrambuf_put(addr,str,6);
      sprintf(str,"%6lu", score[current_player]);
      addr = NTADR_A(6 + nt_x_offset[current_player],27);
      vrambuf_put(addr,str,6);

      //play hit sound
      play_hit();

      //reinit value for next compute
      nb_puyos_destroyed[current_player] = 0;
      mask_color_destroyed = mask_color_destroyed & ((current_player == 0) ? 0xf0 : 0xf) /*0xF0*/;
      nb_group[current_player] = 0;
      step_refresh_ojama_display = 1; // to start the refresh_ojama_display
      break;
  }
}

//Refresh both ojama damage display above player fields
//use step_refresh_ojama_display the step
//if it is reset to 1 then the points have been updates and it needs to be redone from scratch, sadly for the performance and all...
void refresh_ojama_display()
{
  tmp_index =  (step_refresh_ojama_display >= 10) ? 1 : 0;
  current_current_damage_tiles = &current_damage_tiles[tmp_index][0];

  switch (step_refresh_ojama_display)
  {
    case 1: //compute the list of tile damage over opponent board
    case 10:
      //the number of ojama depends of the score
      //see https://www.bayoen.fr/wiki/Tableau_des_dommages
      //would be neater to put addresses into an enum...
      //1:    0xfc   ojama
      //6:    0xf8   big ojama
      //30:   0xe4   rock
      //180:  0xe8   tilted rock
      //360:  0xec   star
      //720:  0xf0   crown
      //1440: 0xf4   comet

      //first let's get our score divided by 70
      tmp_score[tmp_index] = ojamas[(tmp_index == 0 ? 2 : 0)] / 70;
      //tmp_score = (ojamas[(current_player == 0 ? 2 : 0)] * 936) >> 16; //
      //36 => 512 + 256 + 128 + 32 +8
      //tmp_score = ojamas[(current_player == 0 ? 2 : 0)];
      //tmp_score = (tmp_score << 9 + tmp_score << 8 + tmp_score << 7 + tmp_score << 5 + tmp_score << 3) >> 16;
      //gp_j = 0;
      //let's cheat, setup everything as ojamaless tile
      memset(current_damage_tiles[tmp_index],bg_tile, sizeof(current_damage_tiles[tmp_index]));
      current_damage_tiles_index[tmp_index] = 0;
      /* bit of explanation here
      * that part is super expensive in term of CPU because of loops + division
      * looks like divisions like eating the NES CPU cycles
      * damageList contained the divider we have to use to select the correct tile
      * 1440, 720, 360, 180, 30, 6, 1
      * first try to optimize: stock the result of tmp_score / damageList[gp_i] in a variable tmp_score2
      * the tmp_score %= damageList[gp_i]; was outside of the if, but it is useless outside, just
      * returning the same value, so it is moved from outside to inside.
      * we are still very near the 30000cy for that part only though...
      * after checking with mesen, we do use more cycles in fact....????!!!!
      * So new technic ! We will do what is explained there:
      *  https://www.embeddedrelated.com/showthread/comp.arch.embedded/29854-1.php
      * The key point here is that you are dividing by a *constant*.  You are 
      *  re-arranging your sum from:
      *          y = x / k
      *  to
      *          y = (x * (2^n / k)) / (2^n)
      *  where n is picked to make the ranges work out well with the sizes of 
      *  arithmetic you are working with.  Since you can use 32-bit arithmetic, 
      *  but only need 16-bit values, make n=16, and you can avoid overflows. 
      *  Thus if you want to divide by 6, you calculate (2^16 / 6) = 0x2aaa, and 
      *  then your "division" becomes a multiply by 0x2aaa, followed by a divide 
      *  by 2^16 (which is just a shift, or 32-bit store followed by a 16-bit load).
      *  If you have a decent C compiler, it will generate such code for you.
      *
      *  So:2^16 is 0xFFFF, 
      *  65535 / 70   => 936, 0x3A8
      *  65535 / 1440 => 45, 0x2D
      *  65535 / 720  => 91, 0x5B
      *  65535 / 360  => 182, 0xB6
      *  65535 / 180  => 364, 0x16C
      *  65535 / 30   => 2184, 0x888
      *  65535 / 6    => 10922, 0x2AAA
      *  note: this can result in an error of 1 sometimes, need to check how to correct that
      *  As first optimisation with that system, we will just hardcode the multiplier in damageListMult
      * result => no change still 29000cy & more used
      * last try before juste splitting into two steps : change the  multiply by bitshifts
      * 936 => 512 + 256 + 128 + 32 +8
      * 45  => 32 + 8 + 4 +1
      * 91  => 64 + 16 + 8 + 2 +1
      * 182 => 128 + 32 + 16 + 4 + 2 
      * 364 => 256 + 64 + 32 + 8 + 4
      * 2184 => 2048 + 128 + 8
      * 10922 => 8192 + 2048 + 512 + 128 + 32 + 8 + 2
      * Ok as nothing works, no I will try the following : if tmp_score < to the divisor value then skip because obviously it will return 0
      * not a big difference, so hammer solution: we will subdivise it into mutiple steps ! from 2 to 3, to 2 to 10
      * /!\ /!\ /!\ /!\ /!\it may cause issues if damage from other player get the points be modified .../!\/!\/!\/!\/!\
      */
      break;
    case 2://1440
    case 3://720
    case 4://360
    case 5://180
    case 6://30
    case 7://6
    case 8://1
    case 11://1440 p2
    case 12://720 p2
    case 13:// 360 p2
    case 14://180 p2
    case 15://30 p2
    case 16://6 p2
    case 17://1 p2
      /*for ( gp_i = 0; gp_i < 7  && gp_j < 6 ; ++gp_i)
      {
        if (tmp_score[current_player] >= damageList[gp_i])
        {
          tmp_score2[current_player] = tmp_score[current_player] / damageList[gp_i];
          //tmp_score2 = tmp_score >> 10-(gp_i); // up to 10000cy better than the line above !
          //tmp_score2 = (tmp_score * damageListMult[gp_i]) >> 16;
          //we go from higher score to lowest, checking the rest.
          if (tmp_score2[current_player] > 0 )
          {
            //we use tmp_mask because it is there, avoiding declaring something else
            for (tmp_mask = 0; tmp_mask < (tmp_score2[current_player]) && gp_j < 6 ; ++tmp_mask)
            {
              current_damage_tiles[current_player][gp_j] = damageTile[gp_i];
              ++gp_j;
            }
            tmp_score[current_player] %= damageList[gp_i]; 
          }
        }      
      }*/
      //we need to compute the correct index for damagelist, we will use gp_i
      gp_i = (step_refresh_ojama_display > 10) ? step_refresh_ojama_display - 11 : step_refresh_ojama_display - 2;
      if (tmp_score[tmp_index] >= damageList[gp_i])
      {
        tmp_score2[tmp_index] = tmp_score[tmp_index] / damageList[gp_i];
         //optimisation following https://embeddedgurus.com/stack-overflow/2011/02/efficient-c-tip-13-use-the-modulus-operator-with-caution/
        // C = A % B is equivalent to C = A – B * (A / B).
        //tmp_score[tmp_index] %= damageList[step_p_counter[tmp_index]];
        tmp_score[tmp_index] -= damageList[gp_i] * (tmp_score2[tmp_index]);
        if (tmp_score2[tmp_index] > 0 )
        {
          //we use tmp_mask because it is there, avoiding declaring something else
          for (tmp_mask = 0; tmp_mask < (tmp_score2[tmp_index]) && current_damage_tiles_index[tmp_index] < 6 ; ++tmp_mask)
          {
            //current_damage_tiles[tmp_index][current_damage_tiles_index[tmp_index]] = damageTile[gp_i];
            current_current_damage_tiles[current_damage_tiles_index[tmp_index]] = damageTile[gp_i];
            ++current_damage_tiles_index[tmp_index];
          }
        }
      }

      if (current_damage_tiles_index[tmp_index] >= 6 )
      {
        // to be sure we don't go in 2 to 8 or 11 to 17  again as no more room for damage tiles
        // the index is 1 below the next step as it is updated below outside the switch case (step_refresh_ojama_display)
        step_refresh_ojama_display = (step_refresh_ojama_display > 10) ? 11 : 8; 
      }
      break;
    case 9:
    case 18://p2
      //display damages
      // if we are updating bg tiles somewhere else then we do not update
      // the state we want to avoid are DESTROY, FALL, POINT,  SHOW_NEXT, POINT and FALL_OJAMA
      // so every step between 4 and 9 included, the missing case on PLAY when sprites are changed to bg will be handled there.
      if (step_p[0] >= 4 && step_p[0] <= 9 && step_p[1] >= 4 && step_p[1] <= 9)
        return;// by returning we con't update the step_refresh_ojama_display step so we will start again from here on the next frame
      
      memset(ntbuf1, 0, sizeof(ntbuf1));
      memset(ntbuf2, 0, sizeof(ntbuf2));

      for (gp_j = 0; gp_j < 6; ++gp_j)
      {
        //set_metatile(gp_j,current_damage_tiles[current_player][gp_j]);
        //set_metatile is only valid for vertical vrambuf_put !
        //for horizontal just fill manually ntbuf1 & 2 :)
       /* ntbuf1[gp_j*2] = current_damage_tiles[tmp_index][gp_j];
        ntbuf1[gp_j*2+1] = current_damage_tiles[tmp_index][gp_j]+2;
        ntbuf2[gp_j*2] = current_damage_tiles[tmp_index][gp_j]+1;
        ntbuf2[gp_j*2+1] = current_damage_tiles[tmp_index][gp_j]+3;*/
        tmp_counter = current_current_damage_tiles[gp_j];
        gp_k = gp_j<<1;
        ntbuf1[gp_k] = tmp_counter;
        ntbuf1[gp_k+1] = tmp_counter+2;
        ntbuf2[gp_k] = tmp_counter+1;
        ntbuf2[gp_k+1] = tmp_counter+3;
        
      }
      tmp_counter = (20)-nt_x_offset[tmp_index]; //no need to compute it twice
      addr = NTADR_A(tmp_counter, 0);
      vrambuf_put(addr, ntbuf1, 12);
      addr = NTADR_A(tmp_counter, 1);
      vrambuf_put(addr, ntbuf2, 12);
      break;
    default: //0 or something else : quit
      break;
  }
  //increment to next step
  if (step_refresh_ojama_display != 0)
    step_refresh_ojama_display++;
  //end of refresh
  if (step_refresh_ojama_display > 18)
    step_refresh_ojama_display = 0;
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
  //byte tmp_counter = step_p_counter[current_player];
  //byte opponent_status = step_p[~current_player & 1]; //~0 & 1 donne 1 et ~1 & 1 donne 0;
  /*byte fall = 0, i = 0, top_line_space = 0;*/
  //byte * offset_address;
  //byte * cell_address;
  offset_address = board_address + (current_player*0x4E) /*+ tmp_counter*0xD*/;

  tmp_counter = 0; // top_line_space
  tmp_counter_2 = step_p[~current_player & 1]; //to save some cycles
  if ((step_ojama_fall[current_player] == 0 && tmp_counter_2 != PLAY && tmp_counter_2 != FALL_OJAMA) )
  {
    //inutile de continuer on passe à SHOW_NEXT
    //step_p[current_player] = SHOW_NEXT;
    //step_p_counter[current_player] = 0;
    //wrong this to go for SHOW_NEXt, because column_height is recomputed in FALL
    //so we won't add the ojama, but we are still doing fall
    // but big issue, FALL leads to CHECK_ALL which lead to FALL_OJAMA, we "fall" into an infinite loop... 
    //SO new state: UPDATE_HEIGHT, will call fall_board as it was from fall_ojama to be able to go to show_next
    step_p[current_player] = UPDATE_HEIGHT;
    step_p_counter[current_player] = 0;
    return 0; //as we return 0 the ojama won't fall
  }
  
  //we need to let the fall_board function fall every column,
  //something it will do in 6 frames
  //so we only add the new ojama to fall on step_p_counter%6 ==0
  // if the step is 0, it is the first time we enter that part, 
  //and there is nothing the fall, so we skip this step
  if (step_ojama_fall[current_player] != 0 && mod_6_lookup[step_p_counter[current_player]] != 0)
  {
    return 1;
  }
  
  //add a line max of ojama on one row 
  if ( (step_ojama_fall[current_player] < 5) && (ojamas[current_player<<1] >= 70))
  {
    tmp_index = current_player<<1;
    cell_address = offset_address;
    if ( ojamas[tmp_index] >= 420)
    {
      // over 70*6, 420, a full line can be added
      for (gp_i = 0; gp_i < 6; ++gp_i)
      {
        if ( (/*boards[current_player][gp_i][0]*/*cell_address) == EMPTY)
        {
          //boards[current_player][gp_i][0] = OJAMA;
          *cell_address = OJAMA;
          if ( (ojamas[tmp_index] - 70) < ojamas[tmp_index]) //if not inferior then less than 70 !
          { 
            ojamas[tmp_index] -= 70;
          }
          else
          {
            ojamas[tmp_index] = 0;
            break;//no points, no need to continue
          }
        }
        cell_address += 0x0D;//pour aller de colonnes en colonne on se déplace de 13
      }
    }
    else
    {
      //less than a line, we have to randomize the popping
      //look for empty spots on the top hidden line:
      tmp_counter = 0;
      for (gp_i = 0; gp_i < 6; ++gp_i )
      {
        if (/*(boards[current_player][gp_i][0])*/*cell_address == EMPTY)
        {
          ++tmp_counter;
        }
        cell_address += 0x0D;//pour aller de colonnes en colonne on se déplace de 13
      }
      if (tmp_counter > 0)
      {
        cell_address = offset_address;
        if (tmp_counter > (ojamas[tmp_index] / 70) )
        {
          gp_i = 0;
          //more space than ojama, we randomize the fall
          while (ojamas[tmp_index] >= 70)
          {
            if (/*(boards[current_player][gp_i][0])*/ *cell_address== EMPTY && (rand8() & 1))
            {
              //boards[current_player][gp_i][0] = OJAMA;
              *cell_address = OJAMA;
              ojamas[tmp_index] -= 70;
            }
            ++gp_i;
            //gp_i = gp_i%6;//to loop
            gp_i = mod_6_lookup[gp_i];
            if (gp_i == 0)
              cell_address = offset_address;//on repart au début de notre tableau, 1ère colonne
            else
              cell_address += 0x0D;//pour aller de colonnes en colonne on se déplace de 13
          }
        }
        else
        {
          //less space than ojama, we fill every holes, the remaining ojamas will fall at next step
          for (gp_i = 0; gp_i < 6; ++gp_i )
          {
            if (/*(boards[current_player][gp_i][0])*/ *cell_address == EMPTY)
            {
              //boards[current_player][gp_i][0] = OJAMA;
              *cell_address = OJAMA;
              ojamas[tmp_index] -= 70;
            }
            cell_address += 0x0D;//pour aller de colonnes en colonne on se déplace de 13
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
  //byte tmp_counter_2, tmp_counter_3, j;
  //register word addr;
  /*byte */tmp_counter = mod_6_lookup[step_p_counter[current_player]];

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
  tmp_cell_address = /*&tmp_boards[tmp_counter][0];*/tmp_boards_address + (tmp_counter*0xF);
  //tmp_boards contains the information we need
  //we start from the bottom, each cell will receive the one above itself
  for (gp_j = 14 ; gp_j > 0 ; --gp_j)
  {
    //tmp_boards[tmp_counter][gp_j] = tmp_boards[tmp_counter][gp_j-1];
    tmp_cell_address[gp_j] = tmp_cell_address[gp_j-1]; //cf 12. of https://www.cc65.org/doc/coding.html
  }
  /*tmp_boards[tmp_counter][14] = tmp_boards[tmp_counter][13];
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
  tmp_boards[tmp_counter][1] = tmp_boards[tmp_counter][0];*/
  //tmp_boards[tmp_counter][0] = EMPTY;  //last one receive empty
  tmp_cell_address[0] = EMPTY;

  
  
  //redraw the column through buffer
  //is it necessary to memset here ?
  //memset(ntbuf1, 0, sizeof(ntbuf1));
  //memset(ntbuf2, 0, sizeof(ntbuf2));
  //memset(attrbuf, 0, sizeof(attrbuf));
  //we start at 1 as we don't want to modify the ceiling
  for (gp_j = 1; gp_j < 15 ; ++gp_j)
  {
    switch (/*tmp_boards[tmp_counter][gp_j]*/ tmp_cell_address[gp_j])
    {
      case EMPTY:
        clear_metatile(gp_j-1);
        attrbuf[gp_j>>1] = return_tile_attribute_color(2,tmp_counter_2,gp_j*2);
        break;
      case OJAMA:
        set_metatile(gp_j-1,0xdc);
        attrbuf[gp_j>>1] = return_tile_attribute_color(0,tmp_counter_2,gp_j*2);
        break;
      /*case PUYO_RED:
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
        break;*/
      case 255: //the wall tile
        set_metatile(gp_j-1, bg_tile);
        attrbuf[gp_j>>1] = return_tile_attribute_color(0,tmp_counter_2,gp_j*2);
        break;
      default:
        //set_metatile(gp_j-1,*(puyoSeq[tmp_boards[tmp_counter][gp_j]+blind_offset]+0x2));
        //attrbuf[gp_j>>1] = return_tile_attribute_color(tmp_boards[tmp_counter][gp_j],tmp_counter_2,gp_j*2);
        set_metatile(gp_j-1,*(puyoSeq[tmp_cell_address[gp_j]+blind_offset]+0x2));
        attrbuf[gp_j>>1] = return_tile_attribute_color(tmp_cell_address[gp_j],tmp_counter_2,gp_j*2);
        break;
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
 
  memset(attrbuf, 0, sizeof(attrbuf));
  //init the displayed_pairs table
  //0x3f is for %64 the cheap way, 
  //See this article on it https://www.patreon.com/posts/50834480
  // so we won't have to take care about looping the index it will be done automatically
  //>>4 and 0xf because 1 color is coded on 4 bits, and a byte contain to puyo, ie one pair

  //the second pair becomes the first pair, the one played, the last pair becomes the 2nd.
  // so we really only have to compute the new third pair
  /*displayed_pairs[current_player][0] = displayed_pairs[current_player][2];
  displayed_pairs[current_player][1] = displayed_pairs[current_player][3];
  displayed_pairs[current_player][2] = displayed_pairs[current_player][4];
  displayed_pairs[current_player][3] = displayed_pairs[current_player][5];*/
 
  gp_i = ((p_puyo_list_index[current_player]>>1)) & 0x3f; //3f == %64
  gp_j = ((p_puyo_list_index[current_player]>>1) + 1) & 0x3f; //3f == %64
  /* example because this is a bit tough
  256 puyos, 128 pairs, 64 bytes in puyo_list
  if puyo list[0] = 0xDA, puyo list[1] = 0x8D
  our first 4 pairs will be : 1101 1010 | 1000 1101
  for index=0 we will display 1101 1010 1000
  for index=1 we will display 1010 1000 1101
  so the third pair is always retrieved from on the (index>>1) + 1 position
  and either from the leftmost puyos if index is even, rightmost is odd.
  the first one always takes index>>1 index and the second depends on parity
  */
  if (p_puyo_list_index[current_player] & 1)
  {
    current_displayed_pairs[0] = (puyo_list[gp_i] & 0xc) >> 2;
    current_displayed_pairs[1] = puyo_list[gp_i] & 0x3;
    current_displayed_pairs[2] = puyo_list[gp_j] >> 6;
    current_displayed_pairs[3] = (puyo_list[gp_j] >> 4) & 0x3;
    //impair, we must take the 4 rightmost bits of p_puyo_list_index[current_player]+1
    current_displayed_pairs[4] = (puyo_list[gp_j] & 0xc) >> 2;
    current_displayed_pairs[5] = puyo_list[gp_j] & 0x3;
  }
  else
  {
    current_displayed_pairs[0] = puyo_list[gp_i] >> 6;
    current_displayed_pairs[1] = (puyo_list[gp_i] >> 4) & 0x3;
    current_displayed_pairs[2] = (puyo_list[gp_i] & 0xc) >> 2;
    current_displayed_pairs[3] = puyo_list[gp_i] & 0x3;
    //pair, we must take the  4 leftmost bits of p_puyo_list_index[current_player]+1
    current_displayed_pairs[4] = puyo_list[gp_j] >> 6;
    current_displayed_pairs[5] = (puyo_list[gp_j] >> 4) & 0x3;
  }
      
  //using the gp_i here curiously does not work, I don't know why
  //looks like it's blocked to a value of 0x2...
  for (gp_i = 0; gp_i < 5; ++gp_i)
  {
    //addr = NTADR_A(14+(current_player<<1), next_columns_y[gp_i]);
    if (gp_i >= 2)
      gp_j = gp_i + 1;
    else
      gp_j = gp_i + 2;
    
    if (gp_i != 2)
    {
      tmp_color = current_displayed_pairs[gp_j];
      set_metatile(gp_i,blind_offset ? *(puyoSeq[tmp_color+blind_offset]+0x2) : 0xc8);//for not blind gamer the tile is different from standard puyos
    }
    else
    {
      tmp_color = bg_pal & 3; // bg_pal is for 4 tiles, we just need the color, so &3 will keep the 2 LSB having the color
      set_metatile(gp_i,bg_tile);//for not blind gamer the tile is different from standard puyos
    }
    attrbuf[gp_i>>1] = return_tile_attribute_color(tmp_color,14+(current_player<<1),next_columns_y[gp_i]); 
  }
 
  //without loop ? prg size issue ? TEst to do=> apaprently not enough space...

  addr = NTADR_A(14+(current_player<<1), next_columns_y[0]);
  vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 10);
  vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 10);
  put_attr_entries((nt2attraddr(addr)), 3); // need to check that 3 at some point...
  
  return;
}

void build_menu()
{
   memset(attribute_table,bg_pal,sizeof(attribute_table));
  // copy attribute table from PRG ROM to VRAM
  vram_write(attribute_table, sizeof(attribute_table));
  
  put_str(NTADR_C(11,13), "Puyo VNES");
  put_str(NTADR_C(4,15), "Border style  1  2  3  4");
  put_str(NTADR_C(4,17), "Border color  1  2  3  4");
  put_str(NTADR_C(4,19), "Music         O  1      ");
  put_str(NTADR_C(4,21), "Color Blind Mode  0  1");
  put_str(NTADR_C(6,24), "Press start to begin!");
  put_str(NTADR_C(9,26), "Alpha v20210825");
  
  //logo white points
  for (i = 2; i < 12; ++i)
  {
    for (j = 0; j < 3; ++j)
    {
      x=4+(8*j);
      vram_adr(NTADR_C(x,i));
      gp_i = logo_whites[i-2][j];
      for (gp_j = 0; gp_j < 8; ++gp_j)
      {
        if ((gp_i & 0x80) == 0x80)
          vram_put(0x01); //l'adresse avance toute seule, rien a faire en théorie
        else
          //il faut faire avancer l'adresse pour aller au prochain bit
          vram_adr(NTADR_C(x+gp_j+1,i));
        gp_i <<= 1; //Bitshift pour tester le bit suivant
      }
    }
  }
  
  //logo for red points
  for (i = 3; i< 11; ++i)
  {
    vram_adr(NTADR_C(5,i));
    gp_i = logo_reds[i-3];
    for (gp_j = 0; gp_j < 8; ++gp_j)
    {
      if ((gp_i & 0x80) == 0x80)
        vram_put(0x03); //l'adresse avance toute seule, rien a faire en théorie
      else
        //il faut faire avancer l'adresse pour aller au prochain bit
        vram_adr(NTADR_C(5+gp_j+1,i));
      gp_i <<= 1; //Bitshift pour tester le bit suivant
    }
  }
  
  //bottom grey of the NES
  vram_adr(NTADR_C(17,10));
  for (i = 0; i<11; ++i)
    vram_put(0x06);
  vram_adr(NTADR_C(18,11));
  for (i = 0; i<9; ++i)
    vram_put(0x06);
  //cap
  vram_adr(NTADR_C(18,8));
  for (i = 0; i<6; ++i)
    vram_put(0xA0);
  vram_adr(NTADR_C(18,9));
  for (i = 0; i<6; ++i)
    vram_put(0xA1);
  vram_adr(NTADR_C(17,8));
  vram_put(0xA3);
  vram_adr(NTADR_C(17,9));
  vram_put(0xA3);
  //black
  vram_adr(NTADR_C(24,10));
  vram_put(0x00);vram_put(0x00);
  vram_adr(NTADR_C(24,11));
  vram_put(0x00);vram_put(0x00);
  //vent holes
  for (i = 3; i < 8; ++i)
  {
    vram_adr(NTADR_C(24,i));
    vram_put(0x8C);vram_put(0x8C);
  }
  
  sprintf(str,"ccc 2021");
  vram_adr(NTADR_C(11,27));
  vram_put(0x10);
  vrambuf_put(NTADR_C(12,27),str,10);
 
}

void build_field()
{
  //byte i, x, y;
  //byte x, y; //déjà déclaré globalement et utilisé pour le x et y du CHECK
  //Filling up boards with EMPTY => done on wait now
  /*for (x = 0; x < 6; ++x)
  {
    for (y = 0; y < 13; ++y)
    {
      boards[0][x][y] = EMPTY;
      boards[1][x][y] = EMPTY;
      tmp_boards[x][y] = 0;
    }
  }*/
  
  //initialize attribute table to 0;
  //0 color palette 0
  //85 color palette 1 (4 couleur par octets, 0b01010101)
  //170 color palette 2 0b10101010
  //255 color palette 3 0b11111111
  memset(attribute_table,bg_pal,sizeof(attribute_table));
  for (x = 0; x < PLAYCOLUMNS; x+=2)
  {
    if (x == 0 || x == 30)
    {
      for (y = 0; y < PLAYROWS; y+=2)
      {
        vram_adr(NTADR_A(x,y));
        vram_put(bg_tile);
        vram_put(bg_tile+2);
        vram_adr(NTADR_A(x,y+1));
        vram_put(bg_tile+1);
        vram_put(bg_tile+3);
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
          vram_put(bg_tile);
          vram_put(bg_tile+2);
          vram_adr(NTADR_A(x,y+1));
          vram_put(bg_tile+1);
          vram_put(bg_tile+3);
        }
      }
    }
    else
    {//le haut/ bas de l'air de jeu
      vram_adr(NTADR_A(x,0));
      vram_put(bg_tile);
      vram_put(bg_tile+2);
      vram_adr(NTADR_A(x,1));
      vram_put(bg_tile+1);
      vram_put(bg_tile+3);
      vram_adr(NTADR_A(x,PLAYROWS-4));
      vram_put(bg_tile);
      vram_put(bg_tile+2);
      vram_adr(NTADR_A(x,PLAYROWS-3));
      vram_put(bg_tile+1);
      vram_put(bg_tile+3);
      vram_adr(NTADR_A(x,PLAYROWS-2));
      vram_put(bg_tile);
      vram_put(bg_tile+2);
      vram_adr(NTADR_A(x,PLAYROWS-1));
      vram_put(bg_tile+1);
      vram_put(bg_tile+3);
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
  
  //the address of the boards, will be usefull in fall_board()
  //board_address = &boards[0][0][0];
  //tmp_boards_address = &tmp_boards[0][0];
}

void init_round()
{
  //byte i;
  // initialize actors
  for ( gp_i = 0; gp_i < 2; ++gp_i)
  {
    actor_x[gp_i][0] = start_pos_x[gp_i]/*3*16 ou 11*16*/;
    actor_y[gp_i][0] = start_pos_y[gp_i][0]/*0*16*/;
    actor_dx[gp_i][0] = 0;
    actor_dy[gp_i][0] = 1;
    actor_x[gp_i][1] = start_pos_x[gp_i]/*3*16 ou 11*16*/;
    actor_y[gp_i][1] = start_pos_y[gp_i][1]/*1*16*/;
    actor_dx[gp_i][1] = 0;
    actor_dy[gp_i][1] = 1;
    //p_puyo_list_index[gp_i] = 0;//done in the WAIT state now.
    
    previous_pad[gp_i] = 0;
    input_delay_PAD_A[gp_i] = 0;
    input_delay_PAD_B[gp_i] = 0;
    input_delay_PAD_LEFT[gp_i] = 0;
    input_delay_PAD_RIGHT[gp_i] = 0;
    timer_grace_period[gp_i] = GRACE_PERIOD; 
    counter_falling_back_up[gp_i] = MAX_FALLING_BACK_UP;
    
    step_p[gp_i] = PLAY;
    step_p_counter[gp_i] = 0;
    current_player = gp_i;
    nb_puyos_destroyed[gp_i] = 0;
    nb_group[gp_i] = 0;
    should_destroy[0] = 0;
    should_destroy[1] = 0;
    //update_next();
  }

  //setting column heights for both players
  for (gp_i = 0; gp_i < 6 ; ++gp_i)
  {
    column_height[0][gp_i] = floor_y;
    column_height[1][gp_i] = floor_y;
  }
  
  mask_color_destroyed = 0;
  step_refresh_ojama_display = 0;

  return;
}

// setup PPU and tables
void setup_graphics() {
  ppu_off();
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
  //p1 puyo 0 & 1, p2 puyo 2 & 3, 0 and 2 are the puyo rotating around 1 et 3
  
  //with puyo side to each other, we need only the height of the column below [1]
  gp_k = (current_actor_x[1] >> 4) - pos_x_offset[current_player];
  tmp_counter = current_column_height[gp_k]; // column height below the puyo
  
  gp_i = current_actor_y[1];
  
  if (current_actor_x[0] < current_actor_x[1])
  {
    //[0] is left to [1]
    //tmp_counter_2, indicating the height of the column to the left of [0] is then in gp_k-2, so column 0 or above only accessible for gp_k>=2, otherwise wall height
    if (gp_k >= 2)
      tmp_counter_2 = current_column_height[gp_k-2];
    else
      tmp_counter_2 = 0xF0;
    //same thinking behind the right column, only got one if gp_k != 5
    if (gp_k == 5) //we are at the rightmost column, so the wall is nearby
      tmp_counter_3 = 0xF0; // 0xFO is our test to "max height"=> wall height
    else
      tmp_counter_3 = current_column_height[gp_k+1];
    
    //left/right, shift by 128px, <<7, for p2
    if ( pad&PAD_LEFT && (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY))
    {
      //add a bit of delay before going again to left
      //if ((current_actor_x[0] > (16+(current_player<<7))) && (current_actor_y[0] <= current_column_height[(current_actor_x[0] >> 4) - pos_x_offset[current_player] - 1]))
      if ((gp_i <= (tmp_counter_2 + 1)) && ((tmp_counter_2 < 0xD0 && gp_i < 0xD0) || (gp_i > 0xD0)))
      {
        current_actor_x[0] -= 16;
        current_actor_x[1] -= 16;
        //refresh the values for our next calculous
        --gp_k;
        /*if (gp_k >= 2)
          tmp_counter_2 = current_column_height[gp_k-2];
        else
          tmp_counter_2 = 0xF0;
        tmp_counter_3 = tmp_counter;*/
       //tmp_counter_2 et tmp_counter_3 ne sont pas utilisés dans la suite
        tmp_counter = current_column_height[gp_k];
      }
    }
    else if ( pad&PAD_RIGHT && (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY))
    {
      //if ((current_actor_x[1] < (96+(current_player<<7))) && (current_actor_y[1] <= current_column_height[(current_actor_x[1] >> 4) - pos_x_offset[current_player] + 1]))
      if ((gp_i <= (tmp_counter_3 + 1)) && ((tmp_counter_3 < 0xD0 && gp_i < 0xD0) || (gp_i > 0xD0)))
      {
        current_actor_x[0] += 16;
        current_actor_x[1] += 16;
        //refresh the values for our next calculous
        ++gp_k;
        /*if (gp_k == 5)
          tmp_counter_3 = 0xF0;
        else
          tmp_counter_3 = current_column_height[gp_k+1];
        tmp_counter_2 = current_column_height[gp_k-2];*/
        //tmp_counter_2 et tmp_counter_3 ne sont pas utilisés dans la suite
        tmp_counter = current_column_height[gp_k]; // devrait être égal à tmp_counter_3 si on veut sauver des cycles.
      }
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
      
      current_actor_x[0] += 16;
      current_actor_y[0] += 16;
      //Another thing must be checked : the height column below puyo 0 !
      //we need to raise both puyo above the topmost puyo of the column (or the ground)
      //kind of "ground kick" or "floor" kick
      //seriously not optimized :-/
      //if ((current_actor_y[0] > current_column_height[(current_actor_x[0] >> 4) - pos_x_offset[current_player] + 1]) )
      if (current_actor_y[0] >= tmp_counter + 1)
      {
        current_actor_y[0] = tmp_counter;
        current_actor_y[1] = current_actor_y[0] - 16;
      }
      
    }
    if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
    { 
      //here as puyo[0] < puyo[1] we are at the left, if we press
      //A the puyo will go over the 2nd puyo
      /*actor_dx[current_player][0] = actor_x[current_player][0];*/
      current_actor_y[0] -= 16;
      current_actor_x[0] += 16;
    }   
  }
  else
  {

    if (current_actor_x[0] != current_actor_x[1])
    {
      //[0] is right to [1]
      //tmp_counter_3, indicating the height of the column to the right of [0] is then in gp_k+2, so column 5 or lower only accessible for gp_k<=3, otherwise wall height
      if (gp_k <= 3)
        tmp_counter_3 = current_column_height[gp_k+2];
      else
        tmp_counter_3 = 0xF0;
      //same thinking behind the left column, only got one if gp_k != 0
      if (gp_k == 0) //we are at the rightmost column, so the wall is nearby
        tmp_counter_2 = 0xF0; // 0xFO is our test to "max height"=> wall height
      else
        tmp_counter_2 = current_column_height[gp_k-1];
      
      //actor_x i is more to the right than actor_x i+1
      //going left or right
      if (pad&PAD_LEFT && (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY))
      {
        //if ((actor_x[current_player][1] > (16+(current_player<<7))) && (current_actor_y[1] <= current_column_height[(actor_x[current_player][1] >> 4) - pos_x_offset[current_player] - 1]))
        if ((gp_i <= (tmp_counter_2 + 1)) && ((tmp_counter_2 < 0xD0 && gp_i < 0xD0) || (gp_i > 0xD0)))
        {
          current_actor_x[0] -= 16;
          current_actor_x[1] -= 16;
          --gp_k;
          tmp_counter = current_column_height[gp_k]; //techniquement c'est tmp_counter_2 si on veut sauver des cycles...
        }
      }
      else if (pad&PAD_RIGHT && (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY))
      {
        if ((gp_i <= (tmp_counter_3 + 1)) && ((tmp_counter_3 < 0xD0 && gp_i < 0xD0) || (gp_i > 0xD0)))
        {
          current_actor_x[0] += 16;
          current_actor_x[1] += 16;
          ++gp_k;
          tmp_counter = current_column_height[gp_k];
        }
      }

      //puyo[0] > puyo[1], it's on its right
      if (pad&PAD_B && input_delay_PAD_B[current_player] == 0)
      { 
        //here as puyo[0] > puyo[1] we are at the right, if we press
        //A the puyo will go over the 2nd puyo
       /* actor_dx[current_player][0] = actor_x[current_player][0];*/
        current_actor_y[0] -= 16;
        current_actor_x[0] -= 16;
      }
      if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
      { 
        //here as puyo[0] > puyo[1] we are at the right, if we press
        //A the puyo will go under the 2nd puyo
        /*actor_dx[current_player][0] = actor_x[current_player][0];*/
        current_actor_y[0] += 16;
        current_actor_x[0] -= 16; 
        //Another thing must be checked : the height column below puyo 0 !
        //we need to raise both puyo above the topmost puyo of the column (or the ground)
        //kind of "ground kick" or "floor" kick
        //seriously not optimized :-/
        if (current_actor_y[0] >= tmp_counter + 1)
        {
          current_actor_y[0] = tmp_counter;
          current_actor_y[1] = current_actor_y[0] - 16;
        }
      }   
    }
    else
    {
      //left or right movement with both actor on the same x
      //need to determine which one is the lowest/highest
      //and the column_height of current x, x before and x after
      gp_k = (current_actor_x[0] >> 4) - pos_x_offset[current_player];
      tmp_counter = current_column_height[gp_k]; // column height below the puyo
      
      if (gp_k == 0) //we are at the leftmost column, so the wall is nearby
        tmp_counter_2 = 0xF0; // 0xFO is our test to "max height" => wall height
      else
        tmp_counter_2 = current_column_height[gp_k-1]; //column height left to the puyo
      
      if (gp_k == 5) //we are at the rightmost column, so the wall is nearby
        tmp_counter_3 = 0xF0; // 0xFO is our test to "max height"=> wall height
      else
        tmp_counter_3 = current_column_height[gp_k+1]; 
                             
      if (current_actor_y[0] < current_actor_y[1])
      {  
        gp_i = current_actor_y[1];
        gp_j = 1;// boolean to indicate that 0 is inferior to one
      }
      else
      {
        gp_i = current_actor_y[0];
        gp_j = 0;// boolean to inidicate than 1 is inferior to 0
      }
      
      //as the height as been computed before we don't need to check for wall anymore
      //if (pad&PAD_LEFT && (current_actor_x[0] > (16+(current_player<<7))) && (gp_i <= current_column_height[(current_actor_x[0] >> 4) - pos_x_offset[current_player] - 1]) )
      if (pad&PAD_LEFT && (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY) )
      {
        if ((gp_i <= (tmp_counter_2 + 1)) && ((tmp_counter_2 < 0xD0 && gp_i < 0xD0 ) || (gp_i > 0xD0))) // the + 1 is to compensate the test being value < floor somewhere else, maybe to adjust later
        {
          current_actor_x[0] -= 16;
          current_actor_x[1] -= 16;
        
        //let's update current position values
          tmp_counter_3 = tmp_counter;
          tmp_counter = tmp_counter_2;
          --gp_k;
          if (gp_k == 0) //we are at the leftmost column, so the wall is nearby
            tmp_counter_2 = 0xF0; // 0xFO is our test to "max height" => wall height
          else
            tmp_counter_2 = current_column_height[gp_k-1]; //column height left to the puyo
        }
      }
      //else if (pad&PAD_RIGHT && (current_actor_x[0] < (96+(current_player<<7))) && (gp_i <= current_column_height[(current_actor_x[0] >> 4) - pos_x_offset[current_player] + 1]) )
      else if (pad&PAD_RIGHT && (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY)  )
      {
        if ((gp_i <= (tmp_counter_3 + 1)) && ((tmp_counter_3 < 0xD0 && gp_i < 0xD0) || (gp_i > 0xD0)))
        {
          current_actor_x[0] += 16;
          current_actor_x[1] += 16;
          
          //let's update current position values
          tmp_counter_2 = tmp_counter;
          tmp_counter = tmp_counter_3;
          ++gp_k;
          if (gp_k == 5) //we are at the rightmost column, so the wall is nearby
            tmp_counter_3 = 0xF0; // 0xFO is our test to "max height" => wall height
          else
            tmp_counter_3 = current_column_height[gp_k+1]; //column height left to the puyo
        }
      }
      
      //same x for both puyo
      //B we go on the left, A we go on the right
      if (pad&PAD_B && input_delay_PAD_B[current_player] == 0)
      { 
        //we need to know if puyo[0] is above or below puyo[1]
        // the lowest value is higher on the screen !
        if (gp_j/*actor_y[current_player][0] < actor_y[current_player][1]*/)
        {
          //going from up to left
          ///are we on the side left side?
          /*
            We should add test against the column height if we are not on left
            if column height is < of top puyo then we should wall kick
            if we wall kick we should check right column height, if the column
            is not free then the puyo should swap.
            if top puyo is above column height before turning
            but below after, then we should raised both puyos (floor kick)
          */
          /*
           you can only go left if the lowest puyo is above the "free space" on left column, otherwise it will wall kick
           here gp_i is that lowest
          */
          //if (current_actor_x[0] == (16+(current_player<<7)))
          if ((gp_i <= tmp_counter_2 + 1) && ((tmp_counter_2 < 0xD0 && gp_i< 0xD0) || (gp_i> 0xD0)))
          {
            current_actor_x[0] -= 16;
            current_actor_y[0] += 16;         
          }
          else
          {
            //wall kick, but only if there is space on the right !
            if ((gp_i <= tmp_counter_3 + 1) && ((tmp_counter_3 < 0xD0 && gp_i< 0xD0) || (gp_i> 0xD0)))
            {
              current_actor_x[1] += 16;
              current_actor_y[0] += 16;
            }
            else
            {
              //Can't wall kick ! So we are just inverting colors
              current_actor_y[1] = current_actor_y[0];
              current_actor_y[0] = gp_i; //gp_i was equal to current_actor_y[1] in that scenario 
            }
          }
        }
        else
        {  //going down to right, the test is different, basically if the column is front of the above puyo is free then we can go, otherwise wk
          //so we have to increment gp_i to match with the above puyo, not the lowest
          gp_i -= 16;
          if ((gp_i <= tmp_counter_3 + 1) && ((tmp_counter_3 < 0xD0 && gp_i< 0xD0) || (gp_i> 0xD0)))
          {
            current_actor_x[0] += 16;
            current_actor_y[0] -= 16;   
          }
          else
          {
            //wall kick, but only if there is space on the left !
            if ((gp_i <= tmp_counter_2 + 1) && ((tmp_counter_2 < 0xD0 && gp_i< 0xD0) || (gp_i> 0xD0)))
            {
              current_actor_x[1] -= 16;
              current_actor_y[0] -= 16;
            }
            else
            {
              current_actor_y[1] = current_actor_y[0];
              current_actor_y[0] -= 16/*gp_i*/; //gp_i was equal to current_actor_y[1] in that scenario 
            }
          }
        }
      }
      if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
      { 
        if (gp_j/*actor_y[current_player][0] < actor_y[current_player][1]*/)
        {
          // going from up to right
          /*
           you can only go right if the lowest puyo is above the "free space" on right column, otherwise it will wall kick
           here gp_i is that lowest
          */
          if ((gp_i <= tmp_counter_3 + 1) && ((tmp_counter_3 < 0xD0 && gp_i< 0xD0) || (gp_i> 0xD0)))
          {
            current_actor_x[0] += 16;
            current_actor_y[0] += 16;         
          }
          else
          {
            //wall kick, but only if there is space on the right !
            if ((gp_i <= tmp_counter_2 + 1) && ((tmp_counter_2 < 0xD0 && gp_i< 0xD0) || (gp_i> 0xD0)))
            {
              current_actor_x[1] -= 16;
              current_actor_y[0] += 16;
            }
            else
            {
              //Can't wall kick ! So we are just inverting colors
              current_actor_y[1] = current_actor_y[0];
              current_actor_y[0] = gp_i; //gp_i was equal to current_actor_y[1] in that scenario 
            }
          }
        }
        else
        {
          //going down to left, the test is different, basically if the column is front of the above puyo is free then we can go, otherwise wk
          //so we have to increment gp_i to match with the above puyo, not the lowest
          gp_i -= 16;
          if ((gp_i <= tmp_counter_2 + 1) && ((tmp_counter_2 < 0xD0 && gp_i< 0xD0) || (gp_i> 0xD0)))
          {
            current_actor_x[0] -= 16;
            current_actor_y[0] -= 16;   
          }
          else
          {
            //wall kick, but only if there is space on the right !
            if ((gp_i <= tmp_counter_3 + 1) && ((tmp_counter_3 < 0xD0 && gp_i< 0xD0) || (gp_i> 0xD0)))
            {
              current_actor_x[1] += 16;
              current_actor_y[0] -= 16;
            }
            else
            {
              current_actor_y[1] = current_actor_y[0];
              current_actor_y[0] -= 16/*gp_i*/; //gp_i was equal to current_actor_y[1] in that scenario 
            }
          }
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
    if (pad&PAD_SELECT && pad&PAD_B && pad&PAD_A)
    { 
      soft_reset = 1;
      return;
    }
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
  if (pad&PAD_SELECT)
  {
    play_bayoen();
    ojamas[0] = 210;
  }
  
  previous_pad[current_player] = pad;
}

//check for the lowest column_height and put the pair there
void ia_move()
{
  tmp_counter = current_column_height[0];
  tmp_counter_2 = 0;
  for (gp_i = 1; gp_i <6; ++gp_i)
  {
    if (current_column_height[gp_i] > tmp_counter)
    {
      tmp_counter = current_column_height[gp_i];
      tmp_counter_2 = gp_i;
    }
  }
  tmp_counter = (9 << 4) + (tmp_counter_2 << 4);
  current_actor_x[0]= tmp_counter;
  current_actor_x[1]= tmp_counter;
}

void main(void)
{
  //label for soft reset:
  main_start:
  //register word addr;
  soft_reset = 0; //reinit the soft reset value 
  setup_graphics();
  // draw message  
  /*vram_adr(NTADR_A(2,3));
  vram_write("HELLO BAYOEN", 12);*/
  build_menu();
  build_field();
  generate_rng();
  debug = DEBUG;
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
  /*menu_pos_y[0] = 0;
  menu_pos_y[1] = 0;
  menu_pos_y[2] = 0;
  menu_pos_y[3] = 0;*/
  actor_x[0][0] = 135;
  actor_x[0][1] = 135;
  actor_x[1][0] = 135;
  actor_x[1][1] = 165;
  actor_y[0][0] = 119;
  actor_y[0][1] = 135;
  actor_y[1][0] = 151;
  actor_y[1][1] = 167;
  check_all_column_list[0] = 0;
  check_all_column_list[1] = 0;
  input_delay_PAD_LEFT[0] = 0; //to prevent multiple inputs
  bg_tile = bg_tile_addr[0];
  bg_pal = 0;
  blind_offset = 0;
  
  //pointers
  //the address of the boards, will be usefull in fall_board()
  board_address = &boards[0][0][0];
  tmp_boards_address = &tmp_boards[0][0];
  
  //init score and wins at 0
  memset(score,0,sizeof(score));
  memset(wins,0,sizeof(wins));
  memset(ready,0,sizeof(ready));

  //init sound & music
  apu_init();
  music_ptr = 0;
  play_bayoen();//play only to initialize dmc as on first play the sample doesn't play...
  
  // enable rendering
  ppu_on_all();
  //ppu_wait_frame();
  //scroll(0,240);
  // infinite loop
  while(1) {
    
    
    /*
    // read a character from VRAM.
    // this is tricky because we have to wait
    // for VSYNC to start, then set the VRAM
    // address to read, then set the VRAM address
    // back to the start of the frame.
    byte getchar(byte x, byte y) {
      // compute VRAM read address
      word addr = NTADR_A(x,y);
      // result goes into rd
      byte rd;
      // wait for VBLANK to start
      ppu_wait_nmi();
      // set vram address and read byte into rd
      vram_adr(addr);
      vram_read(&rd, 1);
      // scroll registers are corrupt
      // fix by setting vram address
      vram_adr(0x0);
      return rd;
}*/
  
       //set sound
    if (!music_ptr && music_selected_ptr != NULL) start_music(music_selected_ptr);//=> ne boucle pas si on l'active pas !
    {
      //read next 8 bytes from music to fill music2[]
      /*vram_adr(0x1000);
      vram_read(&music2[0],8);
      vram_adr(0);*/
      //next music 
       play_music();
    }
    
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
        scroll(0,240);//y==256 bottom screen,0 top //wrong, the bottom is 240 !
        /*sprintf(str,"press start");
        addr = NTADR_C(10,15);
        vrambuf_put(addr,str,11);*/
        pad = pad_poll(0);
        if (pad&PAD_START)
        {
          ppu_off();//désactiver le rendering est nécessaire pour éviter les glitchs visuels, mais ça fait une frame noire/léger clignottement
          build_field();
          ppu_on_all();
          step_p_counter[0]=239;
          play_bayoen();
          oam_clear();
          continue;
        }
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
          switch (menu_pos_x)
          {
            case 0: //style
              bg_tile = bg_tile_addr[menu_pos_y[menu_pos_x]];
              break;
            case 1: //palette
              bg_pal = menu_pos_y[menu_pos_x] + (menu_pos_y[menu_pos_x]<<2) + (menu_pos_y[menu_pos_x]<<4) + (menu_pos_y[menu_pos_x]<<6);
              break;
            case 2: //music
              music_selected_ptr = (menu_pos_y[menu_pos_x] & 0x1) ? music1:  NULL;
              music_ptr = music_selected_ptr;
              cur_duration = 0;
              if (menu_pos_y[menu_pos_x] & 0x2)
                debug = 1;
              else
                debug = 0;
              break;
            case 3: //color blind
              blind_offset = (menu_pos_y[menu_pos_x]%2) << 2;
              if (menu_pos_y[menu_pos_x] & 0x2)
                ia = 1;
              else
                ia = 0;
              break;
          }
        }
        if (pad& PAD_LEFT && menu_pos_y[menu_pos_x] > 0 && input_delay_PAD_LEFT[0] == 0)
        {
          --menu_pos_y[menu_pos_x];
          actor_x[menu_pos_x/2][menu_pos_x%2] -= 24;
          input_delay_PAD_LEFT[0] = 8;
          switch (menu_pos_x)
          {
            case 0: //style
              bg_tile = bg_tile_addr[menu_pos_y[menu_pos_x]];
              break;
            case 1: //palette
              bg_pal = menu_pos_y[menu_pos_x] + (menu_pos_y[menu_pos_x]<<2) + (menu_pos_y[menu_pos_x]<<4) + (menu_pos_y[menu_pos_x]<<6);
              break;
            case 2: //music
              music_selected_ptr = (menu_pos_y[menu_pos_x]& 0x1) ? music1:  NULL;
              music_ptr = music_selected_ptr;
              cur_duration = 0;
              if (menu_pos_y[menu_pos_x] & 0x2)
                debug = 1;
              else
                debug = 0;
              break;
            case 3: //color blind
              blind_offset = (menu_pos_y[menu_pos_x]%2) << 2;
              if (menu_pos_y[menu_pos_x] & 0x2)
                ia = 1;
              else
                ia = 0;
              break;
          }
        }
        // menu's sprites
        //  set sprite in OAM buffer, chrnum is tile, attr is attribute, sprid is offset in OAM in bytes
        // returns sprid+4, which is offset for a next sprite
        /*unsigned char __fastcall__ oam_spr(unsigned char x, unsigned char y,
					unsigned char chrnum, unsigned char attr,
					unsigned char sprid);*/
        oam_id = oam_spr(16, actor_y[0][0]+16*menu_pos_x, 0xAE, 0, oam_id );
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
      //current_actor_x points to the current player so we don't have to do a [][] but only a [] which should be faster and avoid
      //some code
      current_actor_x = &actor_x[current_player][0]; //current_actor_x points to the current player x
      current_actor_y = &actor_y[current_player][0]; //current_actor_y points to the current player y
      current_column_height = &column_height[current_player][0];
      current_displayed_pairs = &displayed_pairs[current_player][0];

      if (step_p[current_player] == PLAY && timer_grace_period[current_player] != 255)
      {
        handle_controler_and_sprites();
        //we will registrer the number of puyo without something below them
        tmp_counter = 0 ;
        //let's save some compute time and rom space by saving the current column_height of the two pairs in gp_i and gp_j
        //not optimal still have to compute it one more time in the loop below
        //column_height of actor_x[current_player][0]
        gp_i = current_column_height[(current_actor_x[0] >> 4) - pos_x_offset[current_player]];
        //column_height of actor_x[current_player][1]
        gp_j = current_column_height[(current_actor_x[1] >> 4) - pos_x_offset[current_player]];
        
        for (i = 0 ; i < 2 ; ++i)
        {
          // puyoseq[0] == red, 1 blue, 2  green, 3 yellow, the good one is taken from
          // puyo_list       p1_puyo_list_index
          // cd update_next for displayed_pairs constructions
          //Debug => on bloque p2
          if (debug && current_player != 0)
          {
            --current_actor_y[i];
          }
          else
          {
            current_actor_y[i] = current_actor_y[i];
          }
          
          if (actor_dy[current_player][i] != 0 /*&&  timer_grace_period[current_player] == GRACE_PERIOD*/) 
            current_actor_y[i] += (actor_dy[current_player][i] + ((timer_grace_period[current_player]!=0 && previous_pad[current_player]&PAD_DOWN)? 2 : 0));
          
           //refresh sprites display
          sprite_addr[current_player][i] = current_displayed_pairs[i] + blind_offset;
          oam_id = oam_meta_spr(current_actor_x[i], current_actor_y[i], oam_id, puyoSeq[sprite_addr[current_player][i]]);

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
          if (current_actor_x[0] == current_actor_x[1])
          {
            if (i == 0)
            {
              if (current_actor_y[i] < current_actor_y[1])
                column_height_offset = 16;
            } 
            else
            {
              if (current_actor_y[i] < current_actor_y[0])
                column_height_offset = 16;
            }    
          }
          
          if (/*actor_dy[current_player][i] != 0 &&*/
              (((current_column_height[(current_actor_x[i] >> 4) - pos_x_offset[current_player]] - column_height_offset) < current_actor_y[i]))
             )
          {
            //if one puyo is blocked then both must be as we will start the grace_period
            //actor_dy[current_player][i] = 0; 
            /*actor_dy[current_player][0] = 0; 
            actor_dy[current_player][1] = 0; */
            tmp_counter += 1 << i; //power of two to know if something is below or not, usefull for fall animation
            //pas la peine de réinit la hauteur, on le fera après
            //actor_y[current_player][i] = column_height[current_player][(actor_x[current_player][i] >> 4) - pos_x_offset[current_player]] + column_height_offset;
            
            //on ne changera la hauteur de la colonne qu'une fois les puyos fixés !
            /*
            column_height[current_player][(actor_x[current_player][i]>>4) - pos_x_offset[current_player]] -= 16;
            //Ne fonctionne bizarrement pas ! 
            if ((column_height[current_player][(actor_x[current_player][i]>>4) - pos_x_offset[current_player]] > floor_y)  ||  ((column_height[current_player][(actor_x[current_player][i]>>4) - pos_x_offset[current_player]])  <= 0) )
              column_height[current_player][(actor_x[current_player][i]>>4) - pos_x_offset[current_player]] = 0;
            */
          }
          else
          {
            tmp_counter = tmp_counter;
          }
          
        }
        
        switch (tmp_counter)
        {
          case 0://if 2 puyos are free again, basically meeting a free column, we reset the timer_grace_period
            timer_grace_period[current_player] = GRACE_PERIOD;
            actor_dy[current_player][0] = 1; 
            actor_dy[current_player][1] = 1;
            break;
          case 1://qu'on ait un ou deux puyo de bloqué on bloque les deux.
          case 2:
          case 3:
            if (timer_grace_period[current_player] != 0)
            {
              actor_dy[current_player][0] = 0; 
              actor_dy[current_player][1] = 0; 
              if (previous_pad[current_player]&PAD_DOWN)
              {
                timer_grace_period[current_player] = 0;
                //we need to adjust in case the down button is pressed
                //wip fix ! I hate it ! too much code redundant with above !
                if (current_actor_x[0] == current_actor_x[1])
                {
                  if (current_actor_y[0] < current_actor_y[1])
                  {
                    current_actor_y[1] = gp_j + 1;
                    current_actor_y[0] = current_actor_y[1] - 16;
                  }
                  else
                  {
                    current_actor_y[0] = gp_i + 1;
                    current_actor_y[1] = current_actor_y[0] - 16;
                  }
                }
                else
                {
                  if (tmp_counter == 1)
                  {
                    current_actor_y[0] = gp_i + 1;
                    current_actor_y[1] = current_actor_y[0];
                  }
                  else
                  {
                    current_actor_y[1] = gp_j + 1;
                    current_actor_y[0] = current_actor_y[1];
                  }
                }
              }
              else
                --timer_grace_period[current_player];
            }
            else
            {
              //if the puyo is blocked we animate it, if not it must goes one step lower
              //do we use step_p_counter[current_player] for the animation ? 
              //the fall and animation of each puyo is asynchronous, 2 counters ?
              if ((tmp_counter & 1) && (step_p_counter[current_player] & 0xf) < 7)
              {
                //first puyo is blocked and must be animated
                if ((step_p_counter[current_player] & 0xf) < 3)
                  actor_dy[current_player][0] = 2;
                else if((step_p_counter[current_player] & 0xf) < 6)
                  actor_dy[current_player][0] = -2;
                else
                  actor_dy[current_player][0] = 0;
                ++step_p_counter[current_player];
              }
              else
              {
                //not blocked and steps over 6 ? it has to fall !
                if ((step_p_counter[current_player] & 0xf) < 7)
                  actor_dy[current_player][0] = 2;
              }
              
              if ((tmp_counter & 2) && (step_p_counter[current_player] & 0xf0) < 0x70)
              {
                //second puyo is blocked and must be animated
                //first puyo is blocked and must be animated
                if ((step_p_counter[current_player] & 0xf0) < 0x30)
                  actor_dy[current_player][1] = 2;
                else if ((step_p_counter[current_player] & 0xf0) < 0x60)
                  actor_dy[current_player][1] = -2;
                else
                  actor_dy[current_player][1] = 0;
                step_p_counter[current_player] += 0x10;
              }
              else
              {
                //not blocked and steps over 6 ? it has to fall !
                if ((step_p_counter[current_player] & 0xf0) < 0x70)
                  actor_dy[current_player][1] = 2;
              }
              
              if ( (step_p_counter[current_player] & 0xf) >= 0x7 && (step_p_counter[current_player] & 0xf0) >= 0x70 )
              {
                //when animation is finished go to transformation
                // The 2 puyos are stopped we go to sprite to bg tile conversion,
                //but the animation of the puyos must be stop finished first !
                timer_grace_period[current_player] = 255;
              }
              
            }
            break;
          default: 
            break;
        }
        
        /*if (tmp_counter != 0)
        {
          //qu'on ait un ou deux puyo de bloqué on bloque les deux.
          actor_dy[current_player][0] = 0; 
          actor_dy[current_player][1] = 0; 
          if (previous_pad[current_player]&PAD_DOWN)
            timer_grace_period[current_player] = 0;
          else
            --timer_grace_period[current_player];
        }
        else
        {
          
          timer_grace_period[current_player] = GRACE_PERIOD;
        }*/

        /*if (timer_grace_period[current_player] < GRACE_PERIOD || (actor_dy[current_player][0] == 0 && actor_dy[current_player][1] == 0))
        {
          if (previous_pad[current_player]&PAD_DOWN)
            timer_grace_period[current_player] = 0;
          else
            --timer_grace_period[current_player];
         //commenté pas sûr que ce soit utile....
         // if (actor_x[current_player][1] == 0 && timer_grace_period[current_player] == 0)
           // column_height[current_player][(actor_x[current_player][1]>>4) - pos_x_offset[current_player]] = actor_y[current_player][1];
        }*/

      }
      else
      {
        //we need to move oam_id to not have an offset, should be a better way though...
        //oam_id = oam_meta_spr(0, 0, oam_id, puyoSeq[displayed_pairs[current_player][2] + blind_offset]);
        //oam_id = oam_meta_spr(0, 0, oam_id, puyoSeq[displayed_pairs[current_player][3] + blind_offset]); 
        //We use the behind to set the sprite behind the background and avoir visual glitch.
        //Note: we should use the oam_meta_sprite_pal to do that, but the oam_id is not followed the same way as with oam_meta_spr...
        //behind is the last element of our metasprite list.
        if (timer_grace_period[current_player] != 255)
        {
          oam_id = oam_meta_spr(0, 0, oam_id, puyoSeq[8]);
          oam_id = oam_meta_spr(0, 0, oam_id, puyoSeq[8]);
        }
      }

      //flush step, that's supposing one opponent has lost
      //we are unsing tmp_boards, which is slightly larger than boards table.
      //the two extra lines are for storing what will be displayed on the floor
      if (step_p[current_player] == FLUSH)
      {
        //memset(tmp_boards,0,sizeof(tmp_boards));
        //init step, we copy current boards into tmp_boards, and set the floor tiles too
        if (step_p_counter[current_player] == 255)
        {
          //to prevent visual glitch of screen jumping
          step_refresh_ojama_display = 0; 
          //
          cell_address = board_address + (current_player ? 0x4E:0);
          tmp_cell_address = tmp_boards_address;
          for ( i = 0; i < 6; ++i)
          {
            //loop inserted to gain a few bytes of space.
            for (j = 0; j<13; ++j)
            {
              //tmp_boards[i][j] = boards[current_player][i][j];
              //tmp_boards[i][j] = *cell_address;
              tmp_cell_address[j] = *cell_address;
              // incrementing by 0x0D in the column loop is unecessary, as basically doing +1 at the end of one column move to the beginning of the next
              ++cell_address;
              //cell_address +=1;
            }
            /*tmp_boards[i][0] = boards[current_player][i][0];
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
            tmp_boards[i][12] = boards[current_player][i][12];*/
            //tmp_boards[i][13] = 255; // we use 255 as a way to identify a floor tile
            // tmp_boards[i][14] = 255;
            tmp_cell_address[13] = 255;
            tmp_cell_address[14] = 255;
            tmp_cell_address += 0xF;
          }
        }
        else
        {
          flush();
        }
        
        ++step_p_counter[current_player];
        
        if (step_p_counter[current_player] > 90) // 15 * 6 == 90
        {
          /*step_p[current_player] = WAIT;
          step_p_counter[current_player] = 0;*/
          //both players should habe the same state:
          step_p[0] = WAIT;
          step_p_counter[0] = 0;
          step_p[1] = WAIT;
          step_p_counter[1] = 0;
        }
        continue;
      }
      
      //wait for next round to start, each player must press A button
      if (step_p[current_player] == WAIT)
      {
        if ((step_p[0] == step_p[1]) && current_player == 0) //both are waiting
        {
          switch (step_p_counter[0])
          {
            case 0:
              //here reset the boards
              memset(boards, EMPTY, sizeof(boards));
              memset(tmp_boards, 0, sizeof(tmp_boards));
              //reset the score
              memset(score,0,sizeof(score));
              //reset the ojamas
              memset(ojamas,0,sizeof(ojamas));
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
              
              sprintf(str,"Hit:%2d", nb_hit[1]);
              addr = NTADR_A(18,26);
              vrambuf_put(addr,str,6);
              
              sprintf(str,"%6lu", score[1]);
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
                if (ready[i] != 1)
                {
                  pad = pad_poll(i);
                  if (pad&PAD_A || (debug && i == 1 && ready[0] == 1))
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
               //the new update_next with a loop takes too much time to be started in init_round, so moved here!
              p_puyo_list_index[0] = 0;
              p_puyo_list_index[1] = 0;//peut-être supprimer cet init de init_round du coup
              current_player = 1;
              current_displayed_pairs = &displayed_pairs[current_player][0];
              update_next();
              current_player = 0;
              current_displayed_pairs = &displayed_pairs[current_player][0];
              update_next();
              step_p_counter[0] = 8;
              break;
            case 8:
             init_round();
              continue;//we want to avoid the step_p_counter_increment
            default:
              break;
          }    
          //++step_p_counter[0];
        }
        continue;//no need to evaluate the other possibilities
      }
      
      // ojama fall, only when opponent is in step "PLAY" when counter is at 0
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

      if (step_p[current_player] == FALL || step_p[current_player] == UPDATE_HEIGHT)
      {
        //execute before destroy to avoid doing destroy and fall consecutively
        fall_board();
        continue;
      }

      //update the next pair to come in the middle of the field
      if (step_p[current_player] == SHOW_NEXT)
      { 
        //either the screen is filled and party is over, or we just continue
        if (boards[current_player][2][1] == EMPTY)
        {
          update_next();
          step_p[current_player] = PLAY;
          step_p_counter[current_player] = 0;
          if (!debug && ia && current_player == 1)
            ia_move();
        }
        else
        {
          //end of the road...
          //that player lose !
          step_p[current_player] = FLUSH;
          step_p_counter[current_player] = 255;
          //hide sprites
          memset(actor_x, 254, sizeof(actor_x));
          memset(actor_y, 254, sizeof(actor_y));
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
        x = (current_actor_x[0]>>4) - pos_x_offset[current_player];
        y = ((current_actor_y[0]>>3)+1)>>1;
        should_destroy[current_player] = (check_board() > 0);
        //if both puyo had the same color it's useless to perform the second check....No because of splits !
        /*if ( (boards[current_player][((actor_x[current_player][1]>>3) - 2) >> 1][((actor_y[current_player][1]>>3)+1)>>1] & 8) != 8)
          should_destroy = (check_board( ((actor_x[current_player][1]>>3) - 2) >> 1, ((actor_y[current_player][1]>>3)+1)>>1) > 0) || should_destroy;*/
        if ( (boards[current_player][(current_actor_x[1]>>4) - pos_x_offset[current_player]][((current_actor_y[1]>>3)+1)>>1] & 8) != 8)
        {
          x = (current_actor_x[1]>>4) - pos_x_offset[current_player];
          y = ((current_actor_y[1]>>3)+1)>>1;
          should_destroy[current_player] = (check_board() > 0) || should_destroy[current_player];
        }
        if (should_destroy[current_player])
        {
          step_p_counter[current_player] = 0;
          step_p[current_player] = DESTROY;
          //let's move sprites to not have them on screen when things explode
          //according to https://wiki.nesdev.com/w/index.php/PPU_OAM, we have to put them between EF and FF on Y and F9 anf FF on X
          current_actor_x[0] = 254;
          current_actor_y[0] = 254;
          current_actor_x[1] = 254;
          current_actor_y[1] = 254;
          should_destroy[current_player] = 0;
        }
        else
        {
          current_actor_x[0] = start_pos_x[current_player]/*3*16*/;
          current_actor_y[0] = start_pos_y[current_player][0]/*0*/;
          current_actor_x[1] = start_pos_x[current_player]/*3*16*/;
          current_actor_y[1] = start_pos_y[current_player][1]/*16*/;
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
      //2020/12/14 new idea to improve compute time in some scenario: only check columns that have fallen !
      //with how check_board works, it shouldn't change its result to avoid the untouched columns
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
          //i = step_p_counter[current_player] / 13;
          i = div_13_lookup[step_p_counter[current_player]];
          if ( (check_all_column_list[current_player] & (1<<i)) > 0)
          {
            //j = 12 - (step_p_counter[current_player] % 13);
            //https://embeddedgurus.com/stack-overflow/2011/02/efficient-c-tip-13-use-the-modulus-operator-with-caution/
            //C = A % B is equivalent to C = A – B * (A / B).
            j = 12 - (step_p_counter[current_player] - 13 * (i));
            //if j == 0 the not tested as it is the hidden line above screen
            if (j)
            {
              cell_address = board_address + (current_player?0x4E:0) + (i*0xD) + j;
              //if (((boards[current_player][i][j] & 7) != EMPTY) && ((boards[current_player][i][j] & FLAG) != FLAG))
              if (((*cell_address & 7) != EMPTY) && ((*cell_address & FLAG) != FLAG))
              {
                x = i;
                y = j;
                should_destroy[current_player] = (check_board() > 0) || should_destroy[current_player] ;
              }  
            }
          }
          ++step_p_counter[current_player];

          //i = step_p_counter[current_player] / 13;
          i = div_13_lookup[step_p_counter[current_player]];
          if ( (check_all_column_list[current_player] & (1<<i)) > 0)
          {
            //j = 12 - (step_p_counter[current_player] % 13);
            j = 12 - (step_p_counter[current_player] - 13 * (i));
            if (j)
            {
              cell_address = board_address + (current_player?0x4E:0) + (i*0xD) + j;
              //if (((boards[current_player][i][j] & 7) != EMPTY) && ((boards[current_player][i][j] & FLAG) != FLAG))
              if (((*cell_address & 7) != EMPTY) && ((*cell_address & FLAG) != FLAG))
              {
                x = i;
                y = j;
                should_destroy[current_player] = (check_board() > 0) || should_destroy[current_player] ;
              }
            }
          }
          ++step_p_counter[current_player];

          //i = step_p_counter[current_player] / 13;
          i = div_13_lookup[step_p_counter[current_player]];
          if ( (check_all_column_list[current_player] & (1<<i)) > 0)
          {
            //j = 12 - (step_p_counter[current_player] % 13);
            j = 12 - (step_p_counter[current_player] - 13 * (i));
            if (j)
            {
              cell_address = board_address + (current_player?0x4E:0) + (i*0xD) + j;
              //if (((boards[current_player][i][j] & 7) != EMPTY) && ((boards[current_player][i][j] & FLAG) != FLAG))
              if (((*cell_address & 7) != EMPTY) && ((*cell_address & FLAG) != FLAG))
              {
                x = i;
                y = j;
                should_destroy[current_player] = (check_board() > 0) || should_destroy[current_player] ;
              }
            }
          }
          ++step_p_counter[current_player];
        }
        else
        {
          //test is over, let's destroy if necessary
          if (should_destroy[current_player])
          {
            step_p_counter[current_player] = 0;
            step_p[current_player] = DESTROY;
            should_destroy[current_player] = 0;
          }
          else
          {
            current_actor_x[0] = start_pos_x[current_player]/*3*16*/;
            current_actor_y[0] = start_pos_y[current_player][0]/*0*/;
            current_actor_x[1] = start_pos_x[current_player]/*3*16*/;
            current_actor_y[1] = start_pos_y[current_player][1]/*16*/;
            actor_dy[current_player][0] = 1;
            actor_dy[current_player][1] = 1;
            ++p_puyo_list_index[current_player];
            //step_p1 = SHOW_NEXT;
            step_p[current_player] = FALL_OJAMA;
            step_p_counter[current_player] = 0;
            nb_hit[current_player] = 0;// hit combo counter
          }
          //reinit the column marked to be checked
          check_all_column_list[current_player] = 0;
        }
        continue;
      }

      if ( step_p[current_player] == PLAY /*&& actor_dy[current_player][0] == 0 && actor_dy[current_player][1] == 0 && actor_dx[current_player][0] == 0 && actor_dx[current_player][1] == 0 */&& timer_grace_period[current_player] == 255 )
      {
        //vrambuf_clear();
        memset(ntbuf1, 0, sizeof(ntbuf1));
        memset(ntbuf2, 0, sizeof(ntbuf2));
        memset(attrbuf, 0, sizeof(attrbuf));
        current_actor_y[0] +=2;
        current_actor_y[1] +=2;
        
        if (current_actor_y[0] >= 0x10 && current_actor_y[0] < 0xD0)
        {
          //set_metatile(0,0xd8);
          //test !
          //puyoSeq contient l'adresse des data du sprite, et l'adresse de la tile est à cette adresse +2
          set_metatile(0,*(puyoSeq[sprite_addr[current_player][0]]+0x2));
          //set_attr_entry((((actor_x[0]/8)+32) & 63)/2,0,return_sprite_color(0));
          //attrbuf should take the color for 4 tiles !
          attrbuf[0] = return_tile_attribute_color(return_sprite_color(current_player << 1), current_actor_x[0]>>3,(current_actor_y[0]>>3) /*+1*/);

          addr = NTADR_A((current_actor_x[0]>>3), (current_actor_y[0]>>3) /*+1*/);
          vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
          vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
          vrambuf_put(nt2attraddr(addr), &attrbuf[0], 1);
        }
        //HACK for unknown reason attribute_table is not correctly updated if function return_attribute_color is called twice
        //like here
        //attribute_table[(((actor_y[0]>>3)+1)<<1) + ((actor_x[0]>>3)>>2)] = attrbuf[0];
        //set_metatile(1,0xd8);
        if (current_actor_y[1] >= 0x10 && current_actor_y[1] < 0xD0)
        {
          set_metatile(0,*(puyoSeq[sprite_addr[current_player][1]]+0x2));

          attrbuf[1] = return_tile_attribute_color(return_sprite_color((current_player<<1) + 1), current_actor_x[1]>>3, (current_actor_y[1]>>3) /*+1*/);/*return_sprite_color(1) + return_sprite_color(1)<<2 + return_sprite_color(1) << 4 + return_sprite_color(1) << 6*/;

          addr = NTADR_A((current_actor_x[1]>>3), (current_actor_y[1]>>3) /*+1*/);
          vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
          vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
          vrambuf_put(nt2attraddr(addr), &attrbuf[1], 1);
        }
        //updating the board, if things are done correctly attrbuf contains the color to be used
        //Still need to convert coordinates ! And not overwrite the value for the opponent board !
        update_boards();
        step_p[current_player] = CHECK;
        play_puyo_fix(); //play sound of puyo fixing into position
        timer_grace_period[current_player] = GRACE_PERIOD; 
        //that continue allows to avoid refresh_ojama_display just after and updating too much vrambuf_put at the same time
        continue;
      }
    }
    //refresh ojama display
    if (step_refresh_ojama_display != 0)
      refresh_ojama_display();
    
    if (soft_reset)
      goto main_start;
  }
}

//bach sonate pour violoncelle N°1
const byte music1[]= {
0x16,0x89,0x1d,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x1d,0x89,0x26,0x89,0x1d,0x89,0x16,0x89,0x1d,0x89,0x26,0x89,0x24,0x89/*0xFF*/,0x26,0x89,0x1d,0x89,0x26,0x89,0x1d,0x89,0x16,0x89,0x1f,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x1f,0x89,0x27,0x89,0x1f,0x89,0x16,0x89,0x1f,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x1f,0x89,0x27,0x89,0x1f,0x89,0x16,0x89,0x21,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x21,0x89,0x27,0x89,0x21,0x89,0x16,0x89,0x21,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x21,0x89,0x27,0x89,0x21,0x89,0x16,0x89,0x22,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x26,0x89,0x22,0x89,0x16,0x89,0x22,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x26,0x89,0x21,0x89,0x16,0x89,0x1f,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x1f,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x1a,0x89,0x1d,0x89,0x1c,0x89,0x1a,0x89,0x1c,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x1c,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x24,0x89,0x29,0x89,0x28,0x89,0x29,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1d,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x13,0x89,0x1a,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x1a,0x89,0x22,0x89,0x1a,0x89,0x13,0x89,0x1a,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x1a,0x89,0x22,0x89,0x1a,0x89,0x13,0x89,0x1c,0x89,0x1d,0x89,0x1f,0x89,0x1d,0x89,0x1c,0x89,0x1a,0x89,0x18,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x29,0x89,0x28,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x29,0x89,0x24,0x89,0x29,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x23,0x89,0x1d,0x89,0x20,0x89,0x1f,0x89,0x20,0x89,0x1d,0x89,0x23,0x89,0x1d,0x89,0x26,0x89,0x1d,0x89,0x20,0x89,0x1f,0x89,0x20,0x89,0x1d,0x89,0x23,0x89,0x1d,0x89,0x1b,0x89,0x1f,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x24,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1f,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x24,0x89,0x21,0x89,0x1f,0x89,0x1e,0x89,0x21,0x89,0x1e,0x89,0x21,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x21,0x89,0x1e,0x89,0x21,0x89,0x1e,0x89,0x21,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x21,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x24,0x89,0x21,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x18,0x89,0x16,0x89,0x15,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x15,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x1d,0x89,0x1b,0x89,0x16,0x89,0x1a,0x89,0x20,0x89,0x1f,0x89,0x20,0x89,0x1a,0x89,0x20,0x89,0x1a,0x89,0x16,0x89,0x1a,0x89,0x20,0x89,0x1f,0x89,0x20,0x89,0x1a,0x89,0x20,0x89,0x1a,0x89,0x16,0x89,0x1b,0x89,0x1f,0x89,0x1d,0x89,0x1f,0x89,0x1b,0x89,0x1f,0x89,0x1b,0x89,0x16,0x89,0x1b,0x89,0x1f,0x89,0x1d,0x89,0x1f,0x89,0x1b,0x89,0x1f,0x89,0x1b,0x89,0x16,0x89,0x21,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x21,0x89,0x27,0x89,0x21,0x89,0x16,0x89,0x21,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x21,0x89,0x27,0x89,0x21,0x89,0x16,0x89,0x1d,0x89,0x26,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x18,0x89,0x16,0x89,0x15,0x89,0x13,0x89,0x11,0x89,0x10,0x89,0x18,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x10,0x89,0x18,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x0f,0x89,0x18,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x0f,0x89,0x18,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x0f,0x89,0x18,0x89,0x1d,0x89,0x21,0x89,0x24,0x89,0x28,0x89,0x29,0x9b,0x18,0x89,0x1a,0x89,0x1b,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x24,0x89,0x21,0x89,0x1d,0x89,0x1f,0x89,0x21,0x89,0x22,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x24,0x89,0x21,0x89,0x22,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x29,0x89,0x2a,0x89,0x29,0x89,0x28,0x89,0x29,0x89,0x29,0x89,0x27,0x89,0x26,0x89,0x27,0x89,0x27,0x89,0x24,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x18,0x89,0x1a,0x89,0x1b,0x89,0x11,0x89,0x18,0x89,0x1d,0x89,0x21,0x89,0x24,0x89,0x26,0x89,0x27,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x16,0x89,0x18,0x89,0x1a,0x89,0x11,0x89,0x16,0x89,0x1a,0x89,0x1d,0x89,0x22,0x89,0x24,0x89,0x26,0x89,0x22,0x89,0x28,0x89,0x25,0x89,0x24,0x89,0x25,0x89,0x25,0x89,0x24,0x89,0x23,0x89,0x24,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x22,0x89,0x22,0x89,0x1f,0x89,0x1c,0x89,0x1a,0x89,0x18,0x89,0x1c,0x89,0x1f,0x89,0x22,0x89,0x24,0x89,0x28,0x89,0x29,0x89,0x28,0x89,0x29,0x89,0x24,0x89,0x21,0x89,0x1f,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x21,0x89,0x18,0x89,0x1d,0x89,0x1c,0x89,0x1a,0x89,0x18,0x89,0x16,0x89,0x15,0x89,0x13,0x89,0x11,0x92,0x27,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x27,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x24,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x1d,0x89,0x1b,0x89,0x1a,0x89,0x18,0x89,0x22,0x89,0x21,0x89,0x1f,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x27,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x27,0x89,0x24,0x89,0x29,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x27,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x27,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x26,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x24,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x22,0x89,0x24,0x89,0x1f,0x89,0x24,0x89,0x21,0x89,0x24,0x89,0x1d,0x89,0x1f,0x89,0x20,0x89,0x1d,0x89,0x21,0x89,0x1d,0x89,0x22,0x89,0x1d,0x89,0x23,0x89,0x1d,0x89,0x24,0x89,0x1d,0x89,0x25,0x89,0x1d,0x89,0x26,0x89,0x1d,0x89,0x27,0x89,0x1d,0x89,0x28,0x89,0x1d,0x89,0x29,0x89,0x1d,0x89,0x2a,0x89,0x1d,0x89,0x2b,0x89,0x1d,0x89,0x2c,0x89,0x1d,0x89,0x2d,0x89,0x1d,0x89,0x2e,0x89,0x26,0x89,0x1d,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x1d,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x2e,0x89,0x26,0x89,0x2e,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x1d,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x2e,0x89,0x24,0x89,0x2d,0x89,0x27,0x89,0x1d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x1d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x2d,0x89,0x27,0x89,0x16,0x26,0x2e,0xff
};

//mod6, need to go up to 90...so let's do stg like 96 + 6
const byte mod_6_lookup[]={0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5};
//mod12 (%12) lookup table!:
const byte mod_12_lookup[]={0,1,2,3,4,5,6,7,8,9,10,11,0,1,2,3,4,5,6,7,8,9,10};
//dividing by 13, must go to 78 at least
const byte div_13_lookup[]={0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
                            2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,
                            4,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,
                            6,6,6,6,6};