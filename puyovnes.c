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
/* RNG, from BluBlue On bayoen discord
* dans le vrai Tsu, le jeu prend 256 puyos répartis équitablement 
* avec les 4 couleurs de la partie, donc 64 de chaque
* il les mélange en les échangeant de place
* la liste qui en résulte est la suite de paires qu'on aura : 
* les deux premiers forment la première paire, etc
*/
// He has also completely deconstruct the RNG of puyo there: https://puyonexus.com/wiki/Puyo_Puyo_Tsu/Random_Number_Generator
// For my own sanity I will build my own RNG based on the principal of 4*64 colors being used 
#define NBCOLORPOOL 64
//256 puyos = 128 pairs...
//an attribute table entry is 2 bits, so 1 puyo = 2bits
//a pair of puyo = 4 bits
//with 8 bits we have 2 pairs
//So we need an array length of 64 bytes/char to stock everything, amazing !
#define PUYOLISTLENGTH 64
#define DEBUG 0 // Used for Training mode now
#define WALL_HEIGHT 20 //for test during the handle_controller stage

/// GLOBAL VARIABLES
byte debug;
byte ia;
byte speed;
byte enable_ac; //AC => All Clear

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

//steps
#define SETUP 0
#define PLAY 1 	         
#define CHECK 2 	
#define CHECK_ALL 3 	
#define DESTROY 4 	
#define FALL 5 	        
#define POINT 6 	
#define SHOW_NEXT 7 	
#define FALL_OJAMA 8 	
#define FLUSH 9 	
#define WAIT 0xA 	
#define UPDATE_HEIGHT 0xB

byte current_player; // takes 0 for P1 or 1 for P2

byte x, y; //position in the board of the puyo to check;

//we will use array for these new to easily switch from p1 to p2
byte step_p[2]; //take a step as defined above.
byte step_p_counter[2]; // counter on the progress in the current step
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

// boards keep the content of each player...board
//[0][0] is top left, [5][12] is bottom right, keeping same axis than display for simplification
byte boards[2][6][13];

//tmp_boards keep temporary information about the boards being analyzed mostly in check_board().
//we bump tmp_boards to 15 for the flush step, it will contain what was the floor before
byte tmp_boards[6][15];

// array of xy coordinates in yyyyxxxx form per bytes, used in check_board
// size of 72 because we have 6*12 visible puyos, and not visible puyos are not checked
byte puyos_to_check[72];

//Pointers to addresses to speed-up access time
byte * board_address;
byte * tmp_boards_address;
byte * cell_address;
byte * cell_address_2;
byte * tmp_cell_address;
byte * tmp_cell_address_2;
byte * current_board_address;
byte * offset_address;
byte * current_actor_x;
byte * current_actor_y;
byte * current_column_height;
byte * current_displayed_pairs;
byte * base_address; // for update_boards
byte * current_current_damage_tiles; // for refresh_ojama_display
byte * gp_address; //general purpose address for random access to table value for instance.
byte * gp_address_2; //general purpose address for random access to table value for instance.



// buffers that hold vertical slices of nametable data
char ntbuf1[PLAYROWS];	// left side
char ntbuf2[PLAYROWS];	// right side

// a vertical slice of attribute table entries
char attrbuf[PLAYROWS/4];

// Puyo sprites
DEF_METASPRITE_2x2(puyo_red, 0xd8, 0 | OAM_BEHIND);//red //OAM_BEHIND is for setting the sprite behind the BG in the attributes
DEF_METASPRITE_2x2(puyo_blue, 0xd8, 1 | OAM_BEHIND);//blue
DEF_METASPRITE_2x2(puyo_green, 0xd8, 2 | OAM_BEHIND);//green
DEF_METASPRITE_2x2(puyo_yellow, 0xd8, 3 | OAM_BEHIND);//yellow
//color blind mode sprites
DEF_METASPRITE_2x2(puyo_heart, 0xcc, 0 | OAM_BEHIND);//red
DEF_METASPRITE_2x2(puyo_rabbit_ghost, 0xd4, 1 | OAM_BEHIND);//blue
DEF_METASPRITE_2x2(puyo_angry, 0xd0, 2 | OAM_BEHIND);//green
DEF_METASPRITE_2x2(puyo_yellow_bis, 0xd8, 3 | OAM_BEHIND);//yellow

//defined by hand to be sure to have the transparent first sprite in the assets
const unsigned char behind[]={\
        0,      0,      0,   OAM_BEHIND, \
        0,      8,      0,   OAM_BEHIND, \
        8,      0,      0,   OAM_BEHIND, \
        8,      8,      0,   OAM_BEHIND, \
        128};

//will list all our puyos to come, generated by RNG
byte puyo_list[PUYOLISTLENGTH];
//each player puyo pair index in the above puyo_list
byte p_puyo_list_index[2];
// this one is stocking in 01 the pair being played and in 23 and 46 the pair to display in next
// so basically the color or each puyo
byte displayed_pairs[2][6];

// actor x/y positions
byte actor_x[3][2];
byte actor_y[3][2];
// actor x/y deltas per frame (signed), so basically the speed at which the puyos are falling
sbyte actor_dx[2][2];
sbyte actor_dy[2][2];

//Variables for damages 
//cf https://www.bayoen.fr/wiki/Tableau_des_dommages for how to compute damages.
byte nb_puyos_destroyed[2]; //how many puyos are destroyed on that hit
byte nb_hit[2];// hit combo counter
byte mask_color_destroyed; // LSB p1, MSB p2, bit mask at 1 for each color present in the hit. bit 0 red, bit 1 blue, bit 2 green, 3 yellow 
byte nb_group[2];//if the group is over 4 puyos add the part over in this variable.
unsigned long int score[2]; //current score of the round
byte has_ac[2]; // has the player made an all clear
byte wins[2]; // number of round won by each player. 
byte ready[2]; //indicates if a player is ok to play the next round
unsigned long int ojamas[4];// 2 pockets of ojama per player, but what is displayed is always the sum of both.
byte step_ojama_fall[2];
byte should_destroy[2]; //also for both players, or there is a risk that they remove the in current check_all result.
byte blind_offset; //offset to apply to get the correct sprite in blind color mode
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

char column_height_offset;

//constant for puyo physics
#define GRACE_PERIOD /*32*/ 64 //time before the puyos fix themselves on the board
#define MAX_FALLING_BACK_UP 8
byte timer_grace_period[2];
byte counter_falling_back_up[2];
#define FLAG 8 // this flag is used to mark puyo to be destroyed in tmp_boards or boards
byte bg_tile;//address of top left, bottom left+1, top right+2, bottom right+4
byte bg_pal;//0 color palette 0, 85 color palette 1 (4 colors per byte, 0b01010101), 170 color palette 2 0b10101010, 255 color palette 3 0b11111111
byte menu_pos_x;
byte menu_pos_y[6];
char str[32];
byte current_damage_tiles[2][6];
byte current_damage_tiles_index[2]; //the table indicate the player, the index is pointing at which current_damage_tiles we will fill.

//indexes only used in main
char i,j;	// actor index
//global indexes and variable to avoid declaring them each time
//gp=>general purpose, sorry I am bad at naming things
byte gp_i, gp_j, gp_k, tmp_counter, tmp_counter_2, tmp_counter_3, tmp_mask, tmp_attr, tmp_index, attr_x, attr_y, tmp_color;
//the height in number of ojama
byte column_height_in_ojama[2], puyo_height_in_ojama[2];

/*register*/ word addr; //the compiler don't want to put that into register, will I lose some speed ?
unsigned long int tmp_score[2], tmp_score2[2];
//variable for fall_board
byte fall, can_fall, first_puyo_found, puyo_found;
// indicates if the damage board needs o be refreshed. 0 = no refresh, 1 and above refresh needed or in progress
byte step_refresh_ojama_display;
// indicates if a soft_reset must be performed
byte soft_reset;
// value incremented by 1 every frame, then & 1 to know if when the player press down is should go down by 2 or 3 px (it alternates every frame)
byte fall_speed;

//global variables for metatiles update
byte metatile_y, metatile_ch;
//for return_sprite_color
byte spr_index;
//for put_attr_entries
byte attr_length;
//for return_tile_attribute_color
byte rta_color, spr_x, spr_y;

// Lookup tables to speed up the / and %  operations, there are filled at the end of the file
const byte div_6_lookup[];
const byte mod_6_lookup[]; //used in fall_board
const byte mod_12_lookup[];
const byte div_13_lookup[];
const byte px_2_puyos[];

//
// MUSIC ROUTINES
//

// Namespace(bias=1.0, freq=111860.8, length=64, maxbits=13, upper=49)
// 440.5 1.79281159771 49
const int note_table_49[64] = {
4304, 4062, 3834, 3619, 3416, 3224, 3043, 2872, 2711, 2559, 2415, 2279, 2151, 2031, 1917, 1809, 1707, 1611, 1521, 1436, 1355, 1279, 1207, 1139, 1075, 1015, 958, 904, 853, 805, 760, 717, 677, 639, 603, 569, 537, 507, 478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253, 239, 225, 213, 201, 189, 179, 168, 159, 150, 142, 134, 126, 119, 112, };


#define NOTE_TABLE note_table_49
//#define BASS_NOTE /*36*/ 12*/

byte music_index = 0;
byte cur_duration = 0;

const byte music1[]; // music data -- see end of file
const byte music2[]; // music data -- see end of file
const byte* music_ptr = NULL;
const byte* music_selected_ptr = NULL;
#define SAMPLE_BAYOEN 0xF800

//end of music bloc

// const something const place data in rom and help save some space in theory
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

//indexes of preview for blind mode
const unsigned char const puyo_preview_blind_index[4] = {0x90,0x94,0x98,0xC8};

//indicates the number of puyo for each ojamas pictures above the field
const unsigned int const damageList[7] = 
{ 
  1440,720,360,180,30,6,1
};

const byte const damageTile[7] = 
{ 
  0xf4,0xf0,0xec,0xe8,0xe4,0xf8,0xfc
};

//starting horizontal position for both puyos
const byte const start_pos_x[2] = 
{
  3*16,11*16
};

//starting horizontal position for both puyos and each player
const byte const start_pos_y[2][2] =
{
 {249,9},
 {249,9}
};
//When translating x position (from 0 to ~256) into column index we >>4 and remove the below offset 
const byte const pos_x_offset[2] = {1,9};
//nametable offset, useful for update tile/nametable/attributes
const byte const nt_x_offset[2] = {2,18};
//shift for color table
const byte const shift[2] = {0,4};
const byte const bg_tile_addr[4] = {0xc4,0x14,0xb0,0xb4};
const byte const floor_y = 192;
const byte next_columns_y[5] = {4,6,8,10,12};
const byte const menu_y_step[6] = {40,40,40,40,24,24};
const byte menu_y_step_nb[6] = {2,2,1,3,3,3};


//menu logo
//white points, just a bit mask for 3 bytes wide (of 8*8 tiles) display of the logo
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

// function declaration help speeding up things
// Most of them they do not take arguments, instead we use global variables, also for speed reasons
word nt2attraddr(void);
void set_metatile(void);
void clear_metatile(void);
void put_attr_entries( void);
void generate_rng(void);
byte return_sprite_color(void);
byte return_tile_attribute_color(void);
void update_boards(void); //return if a puyo has been placed on x = 2 and y = 1, then flush
byte check_board(void);
byte destroy_board(void);
void fall_board(void);
void manage_point(void);
void build_menu(void);
void build_credits(void);
void handle_menu_settings(void);
void build_field(void);
void setup_graphics(void);
void handle_controler_and_sprites(void);
void update_next(void);
//music and sfx related
byte next_music_byte(void);
void play_music(void);
void start_music(const byte* music); //used only outside of gameplay, no need to optimize
void play_rotation_sound(void);
void play_hit(void); //pitch get higher with byte until reaching "bayoen !"
void play_puyo_fix(void); //when puyo is hitting ground, to be changed a bit tamed currently
void play_bayoen(void); // play bayoen sample
void play_flush(void); // flush sound when a player lose
byte fall_ojama(void); //fall ojama damage on the player field
void flush(void); // flush loser screen into under the play field
void init_round(void); //set actors, column_height and other things before a round
void put_str(unsigned int adr, const char *str);// used only outside of gameplay, no need to optimize
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
      if ((note & 0x80) == 0) 
      {
        //we can simplify that as we only use the PULSE for music, not the bass
        //the bass sound good though, may change my mind later...
        // pulse plays higher notes, triangle for lower if it's free
        //if (/*note >= BASS_NOTE ||*/ (chs & 4)) 
        //{
          int period = NOTE_TABLE[note & 63];
          // see which pulse generator is free
          if (!(chs & 1))
          {
            APU_PULSE_DECAY(0, period, DUTY_25, 4, 10);
            chs |= 1;
          } 
          else if (!(chs & 2))
          {
            APU_PULSE_DECAY(1, period, DUTY_25, 4, 10);
            chs |= 2;
          }
        //}
        /*else
        {
          int period = note_table_tri[note & 63];
          APU_TRIANGLE_LENGTH(period, 15);
          chs |= 4;
        }*/
      }
      else
      {
        // end of score marker
        if (note == 0xff)
          music_ptr = NULL;
        // set duration until next note
        cur_duration = ((note-speed) & 63);
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
  APU_NOISE_DECAY(1,8,2);
}

void play_hit()
{
  //PULSE_CH0 is used by music, the sweep can be an issue
  //as it won't be deactivated automatically
  //so we use PULSE_CH1 for the moment
  gp_address = &nb_hit[current_player];
  if ((*gp_address) < 8)
  {
    //multiply by 250 is costly so let's bitshift by 8 which equals to 256, should sound close enough
    //APU_PULSE_DECAY(PULSE_CH1, 2250-(nb_hit[current_player]*250), 192, 8, 1);
    APU_PULSE_DECAY(PULSE_CH1, 2250-((*gp_address)<<8), 192, 8, 1);
    APU_PULSE_SWEEP(PULSE_CH1,4,2,1);
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
  APU_DMC_address(SAMPLE_BAYOEN);
  APU_DMC_length(0x75);
}

void play_flush()
{
  APU_PULSE_DECAY(PULSE_CH1, 963, 128, 10, 1);
  APU_PULSE_SWEEP(PULSE_CH1,6,3,0);
  APU_TRIANGLE_LENGTH(1735,4);
  APU_NOISE_DECAY(10,7,128);
}

//end of music bloc

// convert from nametable address to attribute table address
word nt2attraddr(/*word a*/) {
 /* return (a & 0x2c00) | 0x3c0 |
    ((a >> 4) & 0x38) | ((a >> 2) & 0x07);*/
   return (addr & 0x2c00) | 0x3c0 |
    ((addr >> 4) & 0x38) | ((addr >> 2) & 0x07);
}

// draw metatile into nametable buffers
// y is the metatile coordinate (row * 2)
// ch is the starting tile index in the pattern table
void set_metatile(/*byte y, byte ch*/) {
  ntbuf1[metatile_y*2] = metatile_ch;
  ntbuf1[metatile_y*2+1] = metatile_ch+1;
  ntbuf2[metatile_y*2] = metatile_ch+2;
  ntbuf2[metatile_y*2+1] = metatile_ch+3;
}

void clear_metatile(/*byte y*/)
{
  ntbuf1[metatile_y*2] = 0;
  ntbuf1[metatile_y*2+1] = 0;
  ntbuf2[metatile_y*2] = 0;
  ntbuf2[metatile_y*2+1] = 0;
}

void put_attr_entries(/*word addr, byte length*/) {
   // if bytes won't fit, wait for vsync and flush buffer
  //-4 ? but 3 bytes are copied by length unit
  if (VBUFSIZE-4-(3*attr_length) < updptr) {
    vrambuf_flush();
  }
  
  for (gp_k = 0; gp_k < attr_length; ++gp_k) {
    VRAMBUF_PUT_SHORT(addr, attrbuf[gp_k]);
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
  0x0F,			// screen color

  0x24,0x30,0x16,0x00,	// background palette 0
  0x39,0x30,0x11,0x00,	// background palette 1
  0x15,0x30,0x2A,0x00,	// background palette 2
  0x12,0x30,0x27,0x00,   // background palette 3

  0x24,0x30,0x16,0x00,	// sprite palette 0
  0x2C,0x30,0x11,0x00,	// sprite palette 1
  0x27,0x30,0x2A,0x00,	// sprite palette 2
  0x2D,0x30,0x27	// sprite palette 3
};

//generates the puyos that will be used during gameplay, they will be stock into puyo_list
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
byte return_sprite_color(/*byte spr_index*/)
{
  return (((OAMSprite*)(0x200+(spr_index<<4)))->attr & 0x3);
}

//based on sprite x/y position look for the bg attributes related to it
//then update the attributes with the color passes in parameter
byte return_tile_attribute_color(/*byte color, byte spr_x, byte spr_y*/)
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
    rta_color <<= 4;
  }
  if (attr_x < spr_x) 
  {
    tmp_mask <<= 2;
    rta_color <<= 2;
  }
  // attribute position is y/2 + x/4 where y 2 LSB are 0
  tmp_index = (attr_y<<1) + (spr_x>>2);

  tmp_attr = attribute_table[tmp_index];
  //let's erase the previous attributes for the intended position
  tmp_attr &= ~tmp_mask; //~ bitwise not, so we keep only bit outside of mask from attr
  tmp_attr += rta_color;
  attribute_table[tmp_index] = tmp_attr;
  return tmp_attr;
}

//Update the boards table, once the puyos have stop moving
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
    cell_address = base_address + ((((current_actor_x[0]>>3) - gp_j) >> 1) * 0xD) + gp_i;
    spr_index = current_player<<1;
    *cell_address = return_sprite_color();
  }

  gp_i = (((current_actor_y[1]>>3)+1)>>1);
   if (gp_i == 0xF)
    gp_i =0;
  if (gp_i < 0xD)
  {
    cell_address = base_address + ((((current_actor_x[1]>>3) - gp_j) >> 1) * 0xD) + gp_i;
    spr_index = (current_player<<1)+1;
    *cell_address = return_sprite_color();
  }
  return;
}

// Look for puyo to destroy and flag them as such
byte check_board()
{
  current_board_address = board_address + (current_player?0x4E:0);
  tmp_counter = 0;
  tmp_counter_2 = 0; // counter
  tmp_counter_3 = 0; // destruction
  
  //Note : sizeof boards won't change, could we use a define here instead of computing it each time the function is called ?
  memset(tmp_boards,0,sizeof(tmp_boards));
  cell_address = current_board_address + (x * 0xD) + y;
  current_color = *cell_address;
  
  if (current_color == OJAMA || y < 1 || y > 12)
    return 0;
  
  tmp_cell_address = tmp_boards_address + (x * 0xF) + y;
  *tmp_cell_address = FLAG; // the currently checked puyo is always flagged

  //NEW METHOD THAT I HOPED FASTER AND MORE STRAIGHTFORWARD
  //starting from the current puyo we look up, down, left, right if another puyo has the same color
  //if yes that puyo x,y coordinates are added in a list in a byte, yyyyxxxx (so yyyy>>4 gives y and x & f gives x
  //we have a counter to increment the position in that list
  //once we have finished with the current puyo we looked at the list, if it's size has up we go to the next element
  //and test again and so on until the pointer in the list is above it's size which means no more puyo has been found.
  //Obviously we flag in tmp_boards the puyo of the same color found.
  
  //gp_i => current x
  //gp_j => current_y
  //tmp_counter is the current element checked in the list
  //tmp_counter_2 is the number of element listed (size of the list) and so the number of puyo to potentially destroy
  puyos_to_check[0] = x + (y << 4);
  do 
  {
    gp_k = puyos_to_check[tmp_counter];
    gp_i = gp_k & 0xf; //X
    gp_j = (gp_k >> 4); //y
    cell_address = current_board_address + (gp_i * 0xD) + gp_j;
    tmp_cell_address = tmp_boards_address + (gp_i * 0xF) + gp_j;
    
    //check above
    if (gp_j > 1) 
    {
      cell_address_2 = cell_address - 1;
      tmp_cell_address_2 = tmp_cell_address - 1;
      if (*cell_address_2 == current_color && (*tmp_cell_address_2 != FLAG))
      {
        ++tmp_counter_2;
        puyos_to_check[tmp_counter_2] = gp_i + ((gp_j - 1) << 4);
        *tmp_cell_address_2 = FLAG;
      }
    }
    
    //check below
    if (gp_j < 12) 
    {
      cell_address_2 = cell_address + 1;
      tmp_cell_address_2 = tmp_cell_address + 1;
      if (*cell_address_2 == current_color && (*tmp_cell_address_2 != FLAG))
      {
        ++tmp_counter_2;
        puyos_to_check[tmp_counter_2] = gp_i + ((gp_j + 1) << 4);
        *tmp_cell_address_2 = FLAG;
      }
    }
    
    //check left
    if (gp_i > 0) 
    {
      cell_address_2 = cell_address - 0xD;
      tmp_cell_address_2 = tmp_cell_address - 0xF;
      if (*cell_address_2 == current_color && (*tmp_cell_address_2 != FLAG))
      {
        ++tmp_counter_2;
        puyos_to_check[tmp_counter_2] = (gp_i - 1) + (gp_j << 4);
        *tmp_cell_address_2 = FLAG;
      }
    }
    
    //check right
    if (gp_i < 5) 
    {
      cell_address_2 = cell_address + 0xD;
      tmp_cell_address_2 = tmp_cell_address + 0xF;
      if (*cell_address_2 == current_color && (*tmp_cell_address_2 != FLAG))
      {
        ++tmp_counter_2;
        puyos_to_check[tmp_counter_2] = (gp_i + 1)  + (gp_j << 4);
        *tmp_cell_address_2 = FLAG;
      }
    }
    
    ++tmp_counter;
  } while (tmp_counter <= tmp_counter_2);
 
  
  //we started from 0, so at 3 we have 4 to erase
  if (tmp_counter_2 >= 3)
  {
    //update the variable for point counting
    nb_puyos_destroyed[current_player] += (tmp_counter_2 + 1); //how many puyos are destroyed on that hit
    // LSB p1, MSB p2, bit mask at 1 for each color present in the hit. bit 0 red, bit 1 blue, bit 2 green, 3 yellow
    mask_color_destroyed |= (1 << shift[current_player]) << current_color;  
    nb_group[current_player] += (tmp_counter_2 + 1) - 4;//if the group is over 4 puyos add the part over in this variable.
    
    tmp_counter_3 = 0;
    //copy flag to boards
    //instead of doing 2 loops, one for column and inside it one for rows
    //we will do only once and parse each cell by only increment by one each time
    gp_j = 1;
    cell_address = current_board_address + 1;
    tmp_cell_address = tmp_boards_address + 1;
    //tmp_board has column size of 15 ! we when gp_i will change we must move of 2 step further.
    //we parse every cell, but must not check that are hidden (wherey y=0)
    //this will be checked with gp_j, but we can start from  1 as the first is not to be checked
    for (gp_i = 1; gp_i < 78; ++gp_i)
    { 
      if (*tmp_cell_address == FLAG)
      {
        *cell_address |= FLAG;
        ++tmp_counter_3;
        //quick hack for ojamas: we look around the current element, up done left right, if one is ojama, it's flagged too be destroyed too
        //note : it will probably slow down things a lot do do that :-s
        //look left
        cell_address_2 = cell_address - 0xD;
        if (gp_i>13 && *(cell_address_2) == OJAMA)
          *(cell_address_2) |= FLAG;
        //look right
        cell_address_2 = cell_address + 0xD;
        if (gp_i<65 && *(cell_address_2) == OJAMA)
          *(cell_address_2) |= FLAG;
        //look up
        cell_address_2 = cell_address - 1;
        if (gp_j>1 && *(cell_address_2) == OJAMA)
          *(cell_address_2) |= FLAG;
        //look down
        cell_address_2 = cell_address + 1;
        if (gp_j<12 && *(cell_address_2) == OJAMA)
          *(cell_address_2) |= FLAG;
      } 
 
      ++cell_address;
      ++gp_j;
      ++tmp_cell_address;
      
      if (gp_j == 13) // 13 ? we must go to the next column and avoid row 0, and row 13 and 14 too for tmp_board
      {
        ++gp_i;
        gp_j = 1;
        ++cell_address;
        tmp_cell_address += 3;
      }
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
      metatile_y = 0;
      metatile_ch = 0xe0; //0xe0 == puyo_pop 
      set_metatile(/*0,0xe0*/);
      // we don't want to lose the flag!
      //gp_k used to temporarily store the new board status
      gp_k = PUYO_POP + FLAG;
    }
    else
    {
      metatile_y = 0;
      clear_metatile(/*0*/);   //step 1 we change the puyo_pop to nothing
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
      if ((*cell_address & FLAG) > 0)
      {
        addr = NTADR_A(gp_i, gp_j << 1 );
        vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
        vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
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
  can_fall = 0;
  first_puyo_found = 0;
  puyo_found = 0;
  fall= 0;
  gp_k = 0; // the actual number of puyo found
 
  tmp_counter = step_p_counter[current_player];
  tmp_counter = mod_6_lookup[tmp_counter];
  offset_address = board_address + (current_player*0x4E) + tmp_counter*0xD;
  
  //we go from bottom to top
  for (gp_j = 12 ; gp_j < 255; --gp_j)
  {
    //so we compute the address by hand, much faster than using direct boards[0][0][0] access
    //basically, boards[0][0][0] is at board_address, select the player by moving of 0x4E == 78 case, a 13*6 boards
    // every columne are separated by 0xD (13) and every line by just one
    cell_address = (offset_address + gp_j); //player index missing there !
    gp_i = *cell_address;

    if ((gp_i & 7) == EMPTY) 
    {
      can_fall = 1;
      //register the position of the first empty area, as it will be the first to be modified
      if (first_puyo_found == 0)
        first_puyo_found = gp_j;
    }
    else
    {
      puyo_found = gp_j; // We need to continue to get the height of the uppest puyos
      ++gp_k;
      
      if (!fall && can_fall)
        fall = 1;
    }
      
    if (can_fall)
    {
      //we can fall and we have something not EMPTY => we fall :)
      if ( gp_j != 0 )
        *cell_address = *(cell_address-1);
      else    
       *cell_address = EMPTY;
    }
      
  }
 
  if (fall == 1)
  {
    //this is falling, so we keep that column value to check it during CHECK_ALL step
    check_all_column_list[current_player] = check_all_column_list[current_player] | (1 << tmp_counter);
    //If we got a fall we reset the counter
    step_p_counter[current_player] = tmp_counter;
    //As it fall the height of the column must be lowered, it is done elsewhere
    
   if (current_player != 0)
    {
      tmp_counter_2 = (tmp_counter + 9) << 1; //x for name or attribute table ?
      tmp_counter_3 = tmp_counter + 8;        //x for name or attribute table ? 
    }
    else
    {
      tmp_counter_2 = (tmp_counter + 1) << 1;
      tmp_counter_3 = tmp_counter;
    }
    
    //redraw the column through buffer
    memset(ntbuf1, 0, sizeof(ntbuf1));
    memset(ntbuf2, 0, sizeof(ntbuf2));
    if (step_p[current_player] != FALL_OJAMA)
    {  
      memset(attrbuf, 0, sizeof(attrbuf));
      //we start at 1 as we don't want to modify the ceiling
      for (gp_j = 1; gp_j <= first_puyo_found; ++gp_j)
      {
        cell_address = (offset_address + gp_j); //player index missing there !
        gp_i = *cell_address;
        metatile_y = gp_j - 1;
        spr_x = tmp_counter_2;
        spr_y = gp_j<<1;
        switch (gp_i)
        {
          case EMPTY:
            clear_metatile();
            rta_color = 2;         
            attrbuf[gp_j>>1] = return_tile_attribute_color();
            break;
          case OJAMA:
            metatile_ch = 0xdc;
            set_metatile();
            rta_color = 0;
            attrbuf[gp_j>>1] = return_tile_attribute_color();
            break;//          
          default:
            metatile_ch = *(puyoSeq[gp_i+blind_offset]+0x2);
            set_metatile();
            rta_color = gp_i;
            attrbuf[gp_j>>1] = return_tile_attribute_color();
            break;
        }
      } 

      //fill the nametable buffers and attributes and then put
      addr = NTADR_A(((tmp_counter_3)<<1)+2, 2 );// the buffer contains all the column height ! we start from the top at 2
      //we reuse tmp_counter for the number of tiles to be updated
      tmp_counter = first_puyo_found << 1;
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, tmp_counter);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, tmp_counter);
      addr = nt2attraddr(/*addr*/);
      //the number of attributes is tricky to set.
      //for a full column we have 7, which is 13+1/2
      //1 for row 0 and 1, 2 for 2 and 3 etc 6 for 10 and 11 and 7 for 12
      //so magic formula is (first_puyo_found / 2) + 1
      attr_length = (first_puyo_found >>1) + 1;
      put_attr_entries(/*(nt2attraddr(*//*addr*//*)), 7*/);
    }
    else
    {
      //In FALL_OJAMA we don't need to update the attribute table
      //as all ojama have the same color whatever the palette is
      //we start at 1 as we don't want to modify the ceiling
      for (gp_j = 1; gp_j <= first_puyo_found; ++gp_j)
      {
        cell_address = (offset_address + gp_j);
        gp_i = *cell_address;
        metatile_y = gp_j - 1;
        
        switch ( gp_i)
        {
          case EMPTY:
            clear_metatile();

            break;
          case OJAMA:
            metatile_ch = 0xdc;
            set_metatile();
            break;//          
          default:
            metatile_ch = *(puyoSeq[gp_i+blind_offset]+0x2);
            set_metatile();
            break;
        }
      } 
      //fill the nametable buffers and attributes and then put
      addr = NTADR_A(((tmp_counter_3)<<1)+2, 2 );// the buffer contains all the column height ! we start from the top at 2
      first_puyo_found <<= 1;
      vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, first_puyo_found);
      vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, first_puyo_found);
    }
  }
  else
  {
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
      current_column_height[tmp_counter] = ((puyo_found-1)<< 4);
    }
    
    if (step_p_counter[current_player] == 11)
    {
      if (step_p[current_player] == FALL)
      {
        gp_address = &boards[current_player][0][12];
        if (
          //If all the bottom cells are empty then it is an all clear !
          //we point at [0][12], the bottom of the first column, so next bottom puyo is 13 further cells
          enable_ac &&
          gp_address[0] == EMPTY &&
          gp_address[13] == EMPTY &&
          gp_address[26] == EMPTY &&
          gp_address[39] == EMPTY &&
          gp_address[52] == EMPTY &&
          gp_address[65] == EMPTY        
        )
        { 
          //all clear !
          //change: let's do all the computation in the manage_point
          //As in theory the point should only be update on the next chain
          has_ac[current_player] = 1;

          sprintf(str,"AC");
          addr = NTADR_A(nt_x_offset[current_player] + 4,26);
          vrambuf_put(addr,str,2);
          //the score display will be updated later by manage_point

          //play hit sound
          play_bayoen();

          step_refresh_ojama_display = 1;
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

// Compute the point and update the score plus the ojama on top of opponent board
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

      //you need to raise the score if bonus are null, to avoid a multiply by 0
      if (tmp_score[current_player] == 0)
        tmp_score[current_player] = 1;

      //Now the disappearing puyos
      tmp_score[current_player] = tmp_score[current_player] * ((unsigned long) nb_puyos_destroyed[current_player] * 10);
      
      //let's finish with the all clear
      if (has_ac[current_player])
      {
        tmp_score[current_player] += 2100;
        has_ac[current_player] = 0;
      }

      score[current_player] += tmp_score[current_player];

      //add the opponent ojama removal from current player stack !
      //we need to first remove the tmp_score from ojama to reduce the number of ojama above our head
      //then the rest can be sent to the opponent
  
      gp_i = current_player << 1; //index for ojama[] of current_player 0=>0 1=>2
      gp_j = (((current_player + 1) << 1) & 2) + 1; //index for ojama[] for opponent 0=>3; 1=>1
      //we remove from ojama[a] and then ojama[a+1]
      for (i = 0 ; i < 2; ++i)
      {
        if (ojamas[gp_i+i] > 0)
        {
          if (tmp_score[current_player] >= ojamas[gp_i+i])
          {
            tmp_score[current_player] -= ojamas[gp_i+i];
            ojamas[gp_i+i] = 0;
          }
          else
          {
            ojamas[gp_i+i] -= tmp_score[current_player];
            tmp_score[current_player] = 0;
          }
        }
      }
      //we put the new ojama in the "a+1" slot
      //they will be added to the "a" slot when the player in again in PLAY state.
      ojamas[gp_j] += tmp_score[current_player];
      
     
      break;
    case 1: // refresh display of hit and score, play sound
      sprintf(str,"%2d", nb_hit[current_player]); //refresh the hit counter
      addr = NTADR_A(nt_x_offset[current_player] + 4,26);
      vrambuf_put(addr,str,2);
      sprintf(str,"%6lu", score[current_player]); //refresh the score
      addr = NTADR_A(6 + nt_x_offset[current_player],27);
      vrambuf_put(addr,str,6);

      //play hit sound
      play_hit();

      //reinit value for next compute
      nb_puyos_destroyed[current_player] = 0;
      mask_color_destroyed = mask_color_destroyed & ((current_player == 0) ? 0xf0 : 0xf);
      nb_group[current_player] = 0;
      step_refresh_ojama_display = 1; // to start the refresh_ojama_display
      break;
  }
}

//Refresh both ojama damage display above player fields
//use step_refresh_ojama_display the step index
//if it is reset to 1 then the points have been updates and it needs to be redone from scratch, sadly for the performance and all...
void refresh_ojama_display()
{
  tmp_index = (step_refresh_ojama_display >= 10) ? 1 : 0;
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
      //Note the division by 70 is super costly in cycles, but I didn't found a valid alternative...
      if (tmp_index == 0)
        tmp_score[tmp_index] = (ojamas[2] + ojamas[3]) / 70;
      else
        tmp_score[tmp_index] = (ojamas[0] + ojamas[1]) / 70;

      //let's cheat, setup everything as ojamaless tile
      memset(current_damage_tiles[tmp_index],bg_tile, sizeof(current_damage_tiles[tmp_index]));
      current_damage_tiles_index[tmp_index] = 0;
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
      //we need to compute the correct index for damagelist, we will use gp_i
      gp_i = (step_refresh_ojama_display > 10) ? step_refresh_ojama_display - 11 : step_refresh_ojama_display - 2;
      if (tmp_score[tmp_index] >= damageList[gp_i])
      {
        tmp_score2[tmp_index] = tmp_score[tmp_index] / damageList[gp_i];
        //optimisation following https://embeddedgurus.com/stack-overflow/2011/02/efficient-c-tip-13-use-the-modulus-operator-with-caution/
        // C = A % B is equivalent to C = A – B * (A / B).
        tmp_score[tmp_index] -= damageList[gp_i] * (tmp_score2[tmp_index]);
        if (tmp_score2[tmp_index] > 0 )
        {
          //we use tmp_mask because it is there, avoiding declaring something else
          for (tmp_mask = 0; tmp_mask < (tmp_score2[tmp_index]) && current_damage_tiles_index[tmp_index] < 6 ; ++tmp_mask)
          {
            current_current_damage_tiles[current_damage_tiles_index[tmp_index]] = damageTile[gp_i];
            ++current_damage_tiles_index[tmp_index];
          }
        }
      }

      if (current_damage_tiles_index[tmp_index] >= 6)
      {
        // to be sure we don't go in 2 to 8 or 11 to 17 again as there is no more room for damage tiles
        // the index is 1 below the next step as it is updated below outside the switch case (step_refresh_ojama_display)
        step_refresh_ojama_display = (step_refresh_ojama_display > 10) ? 11 : 8; 
      }
      break;
    case 9:
    case 18://p2
      //display damages
      // if we are updating bg tiles somewhere else then we do not update
      // the state we want to avoid are DESTROY, FALL, POINT, SHOW_NEXT, POINT and FALL_OJAMA
      // so every step between 4 and 9 included, the missing case on PLAY when sprites are changed to bg will be handled there.
      if (step_p[0] >= 4 && step_p[0] <= 9 && step_p[1] >= 4 && step_p[1] <= 9)
        return;// by returning we don't update the step_refresh_ojama_display step so we will start again from here on the next frame
      
      memset(ntbuf1, 0, sizeof(ntbuf1));
      memset(ntbuf2, 0, sizeof(ntbuf2));

      for (gp_j = 0; gp_j < 6; ++gp_j)
      {
        //for horizontal just fill manually ntbuf1 & 2 :)
        tmp_counter = current_current_damage_tiles[gp_j];
        gp_k = gp_j<<1;
        ntbuf1[gp_k] = tmp_counter;
        ntbuf1[gp_k+1] = tmp_counter+2;
        ntbuf2[gp_k] = tmp_counter+1;
        ntbuf2[gp_k+1] = tmp_counter+3;
        
      }
      tmp_counter = (20) - nt_x_offset[tmp_index]; //no need to compute it twice
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
  The other player must be in "PLAY" step when the step_counter of fall_ojama is at 0 => CHANGED !
  We look at ojamas[0] or ojamas[2] as the ojamas that are not ready to fall yet
  Are added to ojamas[1] or ojamas[3] and added juste before the FALL_OJAMA step of the other player
  
  We can only fall 5 rows at a time, then it goes to SHOW_NEXT and player continue to play
  The cell [2][1] must be free, if not game over
  The ojama score should be superior to 0
  */

  offset_address = board_address + (current_player*0x4E);

  tmp_counter = 0; // top_line_space
  tmp_counter_2 = step_p[~current_player & 1]; //to save some cycles,~0 & 1 gives 1 and ~1 & 1 gives 0;

  // we need to let the fall_board function fall every column,
  // something it will do in 6 frames
  // so we only add the new ojama to fall on step_p_counter%6 == 0
  // if the step is 0, it is the first time we enter that part, 
  // and there is nothing the fall, so we skip this step
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
        if ( (*cell_address) == EMPTY)
        {
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
        cell_address += 0x0D; //to move from column to column we add 13, 0xD.
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
        cell_address += 0x0D; //to move from column to column we add 13, 0xD.
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
            if ( *cell_address== EMPTY && (rand8() & 1))
            {
              *cell_address = OJAMA;
              ojamas[tmp_index] -= 70;
            }
            ++gp_i;
            //mod 6 to loop
            gp_i = mod_6_lookup[gp_i];
            if (gp_i == 0)
              cell_address = offset_address;//back to the beginning of our table, first column
            else
              cell_address += 0x0D; //to move from column to column we add 13, 0xD.
          }
        }
        else
        {
          //less space than ojama, we fill every holes, the remaining ojamas will fall at next step
          for (gp_i = 0; gp_i < 6; ++gp_i )
          {
            if (*cell_address == EMPTY)
            {
              *cell_address = OJAMA;
              ojamas[tmp_index] -= 70;
            }
            cell_address += 0x0D; //to move from column to column we add 13, 0xD.
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

  tmp_counter = mod_6_lookup[step_p_counter[current_player]];

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
  tmp_cell_address = tmp_boards_address + (tmp_counter*0xF);
  //tmp_boards contains the information we need
  //we start from the bottom, each cell will receive the one above itself
  for (gp_j = 14 ; gp_j > 0 ; --gp_j)
  {
    tmp_cell_address[gp_j] = tmp_cell_address[gp_j-1]; //cf 12. of https://www.cc65.org/doc/coding.html
  }
  //last one receive empty
  tmp_cell_address[0] = EMPTY;

  //redraw the column through buffer
  //we start at 1 as we don't want to modify the ceiling
  for (gp_j = 1; gp_j < 15 ; ++gp_j)
  {
    metatile_y = gp_j - 1;
    spr_x = tmp_counter_2;
    spr_y = gp_j << 1;
    switch (tmp_cell_address[gp_j])
    {
      case EMPTY:
        clear_metatile();
        rta_color = 2;
        attrbuf[gp_j>>1] = return_tile_attribute_color();
        break;
      case OJAMA:
        metatile_ch = 0xdc;
        set_metatile();
        rta_color = 0;
        attrbuf[gp_j>>1] = return_tile_attribute_color();
        break;
      case 255: //the wall tile
        metatile_ch = bg_tile;
        set_metatile();
        rta_color = 0;
        attrbuf[gp_j>>1] = return_tile_attribute_color();
        break;
      default:
        metatile_ch = *(puyoSeq[tmp_cell_address[gp_j]+blind_offset]+0x2);
        set_metatile();
        rta_color = tmp_cell_address[gp_j];
        attrbuf[gp_j>>1] = return_tile_attribute_color();
        break;
    }
  } 

  //fill the buffers and attributes and put
  addr = NTADR_A(((tmp_counter_3)*2)+2, 2 );// Buffer contains all the the column height ! we start from the top at 2
  vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 28);
  vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 28);
  addr = nt2attraddr(/*addr*/);
  attr_length = 7;
  put_attr_entries(/*(nt2attraddr(*//*addr*//*)), 7*/);
  
  return;
}

//update the color of the next pair to come between fields
void update_next()
{
  memset(attrbuf, 0, sizeof(attrbuf));
  // init the displayed_pairs table
  // 0x3f is for %64 the cheap way, 
  // See this article on it https://www.patreon.com/posts/50834480
  // so we won't have to take care about looping the index it will be done automatically
  // >>4 and 0xf because 1 color is coded on 4 bits, and a byte contain to puyo, i.e one pair

  //the second pair becomes the first pair, the one played, the last pair becomes the 2nd.
  // so we really only have to compute the new third pair
  
  gp_k = p_puyo_list_index[current_player];
  gp_i = ((gp_k>>1)) & 0x3f; //3f == %64
  gp_j = ((gp_k>>1) + 1) & 0x3f; //3f == %64
  /* example because this is a bit tough
  256 puyos, 128 pairs, 64 bytes in puyo_list
  if puyo list[0] = 0xDA, puyo list[1] = 0x8D
  our first 4 pairs will be : 1101 1010 | 1000 1101
  for index==0 we will display 1101 1010 1000
  for index==1 we will display 1010 1000 1101
  so the third pair is always retrieved from on the (index>>1) + 1 position
  and either from the leftmost puyos if index is even, rightmost is odd.
  the first one always takes index>>1 index and the second depends on parity*/
 
  if (gp_k & 1)
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
    
    metatile_y = gp_i;
    
    if (gp_i != 2)
    {
      tmp_color = current_displayed_pairs[gp_j];
      //for not blind gamer the tile is different from standard puyos
      metatile_ch = blind_offset ? puyo_preview_blind_index[tmp_color] : 0xc8;
      set_metatile();
    }
    else
    {
      metatile_ch = bg_tile;
      tmp_color = bg_pal & 3; // bg_pal is for 4 tiles, we just need the color, so &3 will keep the 2 LSB having the color
      set_metatile();
    }
    rta_color = tmp_color;
    spr_x = 14+(current_player<<1);
    spr_y = next_columns_y[gp_i];
    attrbuf[gp_i>>1] = return_tile_attribute_color(); 
  }
 
  addr = NTADR_A(14+(current_player<<1), next_columns_y[0]);
  vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 10);
  vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 10);
  addr = nt2attraddr(/*addr*/);
  attr_length = 3;
  put_attr_entries(/*(nt2attraddr(*//*addr*//*)), 3*/);
  
  return;
}

void build_menu()
{
   memset(attribute_table,bg_pal,sizeof(attribute_table));
  // copy attribute table from PRG ROM to VRAM
  vram_write(attribute_table, sizeof(attribute_table));
  
  put_str(NTADR_C(5,13), "Puyo VNES RC 31/10/2021");
  put_str(NTADR_C(4,15), "Game Mode     1P   2P   Tr");
  put_str(NTADR_C(4,17), "Music         0ff  A    B");
  put_str(NTADR_C(4,19), "Speed         60Hz 50Hz");
  put_str(NTADR_C(4,21), "Color Blind Mode  0ff  On");
  put_str(NTADR_C(4,23), "Border style  1  2  3  4");
  put_str(NTADR_C(4,25), "Border color  1  2  3  4");
  put_str(NTADR_C(6,27), "Press start to begin!");
  
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
          vram_put(0x01); //Address moves itself, nothing to do
        else
          //Address must be moved to go to the next bit
          vram_adr(NTADR_C(x+gp_j+1,i));
        gp_i <<= 1; //Bitshift to test the next bit
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
        vram_put(0x03); //Address moves itself, nothing to do
      else
          //Address must be moved to go to the next bit
        vram_adr(NTADR_C(5+gp_j+1,i));
      gp_i <<= 1; //Bitshift to test the next bit
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
  vram_adr(NTADR_A(11,26));
  vram_put(0x10);
  vrambuf_put(NTADR_A(12,26),str,10);
 
}

//add the credits info in the screen next to menu
void build_credits()
{
  put_str(NTADR_A(7,3), "Puyo VNES credits");
  
  put_str(NTADR_A(3,5), "Created and programmed by");
  put_str(NTADR_A(14,6), "ccc");
  
  put_str(NTADR_A(11,8), "Made with ");
  put_str(NTADR_A(3,9), "https://8bitworkshop.com/");
  put_str(NTADR_A(11,11), "Music by");
  put_str(NTADR_A(6,12), "J.S.BACH - BWV 1007");
  put_str(NTADR_A(3,13), "F.F.Chopin Valse Op.64 No.1");
  put_str(NTADR_A(3,15), "Thanks to family, friends");
  put_str(NTADR_A(5,16), "and the Puyo community");
  put_str(NTADR_A(7,17), "that supported me");
  put_str(NTADR_A(6,18), "during this project");

  put_str(NTADR_A(5,20), "Special Thanks to our");
  put_str(NTADR_A(9,21), "Beta testers");
  put_str(NTADR_A(7,22), "Aurel509   BrouH");
  put_str(NTADR_A(7,23), "Hiku       LIWYC");
  put_str(NTADR_A(7,24), "Toti       Sirix");
}

//the build_field will take several steps
//first a fade_in,
//Then ppu_off, draw the field and ppu_on
//the fade_out
//it will take several frame to be achieved
void build_field()
{
  //initialize attribute table to 0;
  //0 color palette 0
  //85 color palette 1 (4 colors per byte, 0b01010101)
  //170 color palette 2 0b10101010
  //255 color palette 3 0b11111111
  if (step_p_counter[1] < 5)
  {
    pal_bright(4-step_p_counter[1]);
  }
  else if (step_p_counter[1] == 5)
  {
    ppu_off();
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
      {//top and bottom
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
        //the 2 playfields
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
    ppu_on_all();
 }
  else if(step_p_counter[1] < 10)
  {
    pal_bright(step_p_counter[1]-5);
  }
  else
  {
    step_p_counter[0] = 2;
  }
  ++step_p_counter[1];
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
    should_destroy[gp_i] = 0;
    has_ac[gp_i] = 0;
    //reinit everything point/score/ojama related
    nb_hit[gp_i] = 0;
    score[gp_i] = 0;
    ojamas[gp_i<<1] = 0;
    ojamas[(gp_i<<1) + 1] = 0;
    check_all_column_list[gp_i] = 0;
    tmp_score[gp_i] = 0;
    tmp_score2[gp_i] = 0;
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
  gp_k = (current_actor_x[1] >> 4) - pos_x_offset[current_player]; //column_index
  tmp_counter = px_2_puyos[current_column_height[gp_k] >> 4]; // column height below the puyo expressed in puyo
  
  gp_i = px_2_puyos[current_actor_y[1] >> 4]; // puyo height expressed in puyo
  
  if (current_actor_x[0] < current_actor_x[1])
  {
    //[0] is left to [1]
    //tmp_counter_2, indicating the height of the column to the left of [0] is then in gp_k-2, so column 0 or above only accessible for gp_k>=2, otherwise wall height
    if (gp_k >= 2)
      tmp_counter_2 = px_2_puyos[current_column_height[gp_k-2] >> 4];
    else
      tmp_counter_2 = WALL_HEIGHT;//20 is above anything, the value itself doesn't really matter
    //same thinking behind the right column, only got one if gp_k != 5
    if (gp_k == 5) //we are at the rightmost column, so the wall is nearby
      //tmp_counter_3 = our test to "max height"=> wall height
      tmp_counter_3 = WALL_HEIGHT; 
    else
      tmp_counter_3 = px_2_puyos[current_column_height[gp_k+1] >> 4]; //  we should store the column_height as puyos number to avoid conversion
    
    //left/right, shift by 128px, <<7, for p2
    if ( pad&PAD_LEFT && (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY))
    {
      if (gp_i > tmp_counter_2)
      {
        current_actor_x[0] -= 16;
        current_actor_x[1] -= 16;
        //refresh the values for our next calculous
        --gp_k;
       //tmp_counter_2 et tmp_counter_3 not used after that point
        tmp_counter = px_2_puyos[current_column_height[gp_k] >> 4];
      }
    }
    else if ( pad&PAD_RIGHT && (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY))
    {
      if (gp_i > tmp_counter_3)
      {
        current_actor_x[0] += 16;
        current_actor_x[1] += 16;
        //refresh the values for our next calculous
        ++gp_k;
        tmp_counter = px_2_puyos[current_column_height[gp_k] >> 4];
      }
    }

    //buttons, the puyo rotating is always the one at the top
    //so with index at 0 (0 p1, 2 p2)
    if (pad&PAD_B && input_delay_PAD_B[current_player] == 0)
    { 
      //here as puyo_x[0] < puyo_x[1] we are at the left, if we press
      //B the puyo will go under the 2nd puyo
      //the delay has to be at 0, because we don't want it to turn automatically
      //you have to press each time              
      current_actor_x[0] += 16;
      current_actor_y[0] += 16;
      //Another thing must be checked : the height column below puyo 0 !
      //we need to raise both puyo above the topmost puyo of the column (or the ground)
      //kind of "ground kick" or "floor" kick
      if (px_2_puyos[current_actor_y[0] >> 4] <= tmp_counter)
      {
        current_actor_y[0] -= 16;
        current_actor_y[1] = current_actor_y[0] - 16;
      }
      
    }
    else if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
    { 
      //here as puyo[0] < puyo[1] we are at the left, if we press
      //A the puyo will go over the 2nd puyo
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
        tmp_counter_3 = px_2_puyos[current_column_height[gp_k+2] >> 4];
      else
        tmp_counter_3 = WALL_HEIGHT;
      //same thinking behind the left column, only got one if gp_k != 0
      if (gp_k == 0) //we are at the rightmost column, so the wall is nearby
        tmp_counter_2 = WALL_HEIGHT; // 20 is not reachable so perfect for wall height
      else
        tmp_counter_2 = px_2_puyos[current_column_height[gp_k-1] >> 4];
      
      //actor_x i is more to the right than actor_x i+1
      //going left or right
      if (pad&PAD_LEFT && (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY))
      {
        if (gp_i > tmp_counter_2)
        {
          current_actor_x[0] -= 16;
          current_actor_x[1] -= 16;
          --gp_k;
          tmp_counter = px_2_puyos[current_column_height[gp_k] >> 4]; //techniquement c'est tmp_counter_2 si on veut sauver des cycles...
        }
      }
      else if (pad&PAD_RIGHT && (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY))
      {
        if (gp_i > tmp_counter_3)
        {
          current_actor_x[0] += 16;
          current_actor_x[1] += 16;
          ++gp_k;
          tmp_counter = px_2_puyos[current_column_height[gp_k] >>4 ];
        }
      }

      //puyo[0] > puyo[1], it's on its right
      if (pad&PAD_B && input_delay_PAD_B[current_player] == 0)
      { 
        //here as puyo[0] > puyo[1] we are at the right, if we press
        //A the puyo will go over the 2nd puyo
        current_actor_y[0] -= 16;
        current_actor_x[0] -= 16;
      }
      else if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
      { 
        //here as puyo[0] > puyo[1] we are at the right, if we press
        //A the puyo will go under the 2nd puyo
        current_actor_y[0] += 16;
        current_actor_x[0] -= 16; 
        //Another thing must be checked : the height column below puyo 0 !
        //we need to raise both puyo above the topmost puyo of the column (or the ground)
        //kind of "ground kick" or "floor" kick
        if (px_2_puyos[current_actor_y[0] >> 4] <= tmp_counter)
        {
          current_actor_y[0] -= 16;
          current_actor_y[1] = current_actor_y[0] - 16;
        }
      }   
    }
    else
    {
      //left or right movement with both actor on the same x
      //need to determine which one is the lowest/highest
      //and the column_height of current x, x before and x after
      gp_k = (current_actor_x[0] >> 4) - pos_x_offset[current_player]; //column_index
      tmp_counter = px_2_puyos[current_column_height[gp_k] >> 4]; // column height below the puyo
      
      if (gp_k == 0) //we are at the leftmost column, so the wall is nearby
        tmp_counter_2 = WALL_HEIGHT; // 20 is our test to "max height" => wall height
      else
        tmp_counter_2 = px_2_puyos[current_column_height[gp_k-1] >> 4]; //column height left to the puyo
      
      if (gp_k == 5) //we are at the rightmost column, so the wall is nearby
        tmp_counter_3 = WALL_HEIGHT; // 20 is our test to "max height"=> wall height
      else
        tmp_counter_3 = px_2_puyos[current_column_height[gp_k+1] >> 4]; 
      
      // the lower is the one to check/keep
      if (px_2_puyos[current_actor_y[0] >> 4] < px_2_puyos[current_actor_y[1] >> 4])
      {
        gp_i = px_2_puyos[current_actor_y[0] >> 4]; //1 is above 0
        gp_j = 0;// boolean to indicate that 1 is above 0
      }
      else
      {
        gp_i = px_2_puyos[current_actor_y[1] >> 4];  //1 is below 0
        gp_j = 1;// boolean to indicate that 1 is below 0
      }
      
      //as the height as been computed before we don't need to check for wall anymore
      if (pad&PAD_LEFT && (input_delay_PAD_LEFT[current_player] == 0 || input_delay_PAD_LEFT[current_player] > INPUT_DIRECTION_DELAY) )
      {
        if (gp_i > tmp_counter_2)
        {
          current_actor_x[0] -= 16;
          current_actor_x[1] -= 16;
        
        //let's update current position values
          tmp_counter_3 = tmp_counter;
          tmp_counter = tmp_counter_2;
          --gp_k;
          if (gp_k == 0) //we are at the leftmost column, so the wall is nearby
            tmp_counter_2 = WALL_HEIGHT;
          else
            tmp_counter_2 = px_2_puyos[current_column_height[gp_k-1] >> 4]; //column height left to the puyo
        }
      }
      else if (pad&PAD_RIGHT && (input_delay_PAD_RIGHT[current_player] == 0 || input_delay_PAD_RIGHT[current_player] > INPUT_DIRECTION_DELAY) )
      {
        if (gp_i > tmp_counter_3)
        {
          current_actor_x[0] += 16;
          current_actor_x[1] += 16;
          
          //let's update current position values
          tmp_counter_2 = tmp_counter;
          tmp_counter = tmp_counter_3;
          ++gp_k;
          if (gp_k == 5) //we are at the rightmost column, so the wall is nearby
            tmp_counter_3 = WALL_HEIGHT;
          else
            tmp_counter_3 = px_2_puyos[current_column_height[gp_k+1] >> 4]; //column height left to the puyo
        }
      }
      
      //same x for both puyo
      //B we go on the left, A we go on the right
      if (pad&PAD_B && input_delay_PAD_B[current_player] == 0)
      { 
        //we need to know if puyo[0] is above or below puyo[1]
        if (gp_j)
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
          if (gp_i > tmp_counter_2)
          {
            current_actor_x[0] -= 16;
            current_actor_y[0] += 16;         
          }
          else
          {
            //wall kick, but only if there is space on the right !
            if (gp_i > tmp_counter_3)
            {
              current_actor_x[1] += 16;
              current_actor_y[0] += 16;
            }
            else
            {
              //Can't wall kick ! So we are just inverting colors
              current_actor_y[1] = current_actor_y[0];
              current_actor_y[0] += 16; // 0 was above and need to go lower so we add 16 
            }
          }
        }
        else
        {  //going down to right, the test is different, basically if the column is front of the above puyo is free then we can go, otherwise wk
          //so we have to increment gp_i to match with the above puyo, not the lowest
          //gp_i -= 16;
          ++gp_i;// raising the puyo height from below
          if (gp_i > tmp_counter_3)
          {
            current_actor_x[0] += 16;
            current_actor_y[0] -= 16;   
          }
          else
          {
            //wall kick, but only if there is space on the left !
            if (gp_i > tmp_counter_2)
            {
              current_actor_x[1] -= 16;
              current_actor_y[0] -= 16;
            }
            else //inverting colors
            {
              current_actor_y[1] = current_actor_y[0];
              current_actor_y[0] -= 16; // 0 is going above 1, so pixel value is reduced
            }
          }
        }
      }
      else if (pad&PAD_A && input_delay_PAD_A[current_player] == 0)
      { 
        if (gp_j) //1 is below ?
        {
          // going from up to right
          /*
           you can only go right if the lowest puyo is above the "free space" on right column, otherwise it will wall kick
           here gp_i is that lowest
          */
          if (gp_i > tmp_counter_3)
          {
            current_actor_x[0] += 16;
            current_actor_y[0] += 16;         
          }
          else
          {
            //wall kick, but only if there is space on the right !
            if (gp_i > tmp_counter_2)
            {
              current_actor_x[1] -= 16;
              current_actor_y[0] += 16;
            }
            else
            {
              //Can't wall kick ! So we are just inverting colors
              current_actor_y[1] = current_actor_y[0];
              current_actor_y[0] += 16; //from up to down
            }
          }
        }
        else
        {
          //going down to left, the test is different, basically if the column is front of the above puyo is free then we can go, otherwise wk
          //so we have to increment gp_i to match with the above puyo, not the lowest
          ++gp_i;
          if (gp_i > tmp_counter_2)
          {
            current_actor_x[0] -= 16;
            current_actor_y[0] -= 16;   
          }
          else
          {
            //wall kick, but only if there is space on the right !
            if (gp_i > tmp_counter_3)
            {
              current_actor_x[1] += 16;
              current_actor_y[0] -= 16;
            }
            else
            {
              current_actor_y[1] = current_actor_y[0];
              current_actor_y[0] -= 16;// 0 move up so it's value is reduced
            }
          }
        }   
      } 
    }
  }
  //play rotation sound if button pressed
  if ((pad&PAD_A || pad&PAD_B) && (previous_pad[current_player] != PAD_A && previous_pad[current_player] != PAD_B))
    play_rotation_sound();
  
  //give up the round
  if (pad&PAD_START)
  {
    if (pad&PAD_SELECT && pad&PAD_B && pad&PAD_A)
    { 
      soft_reset = 1;
      return;
    }
    play_flush();
    step_p[current_player] = FLUSH;
    step_p_counter[current_player] = 255;
    actor_dx[current_player ? 0 : 1][0] = -1;
    ++wins[current_player ? 0 : 1];
  }
  
  previous_pad[current_player] = pad;
}

void handle_menu_settings()
{
  switch (menu_pos_x)
  {
    case 0: //Game mode
      switch (menu_pos_y[menu_pos_x])
      {
        case 0: 
          ia = 1;
          debug = 0;
          break;
        case 1: 
          ia = 0;
          debug = 0;
          break;
        case 2:
          ia = 0;
          debug = 1;
          break;
      }
      break;
    case 1: //music
      switch (menu_pos_y[menu_pos_x])
      {
        case 0: 
          music_selected_ptr = NULL;
          break;
        case 1: 
          music_selected_ptr = music1;
          break;
        case 2:
          music_selected_ptr = music2;
          break;
      }
      music_ptr = music_selected_ptr;
      cur_duration = 0;
      break;
    case 2:  //game speed
      speed = menu_pos_y[menu_pos_x];
      break;
    case 3: //color blind
      blind_offset = (menu_pos_y[menu_pos_x]%2) << 2;
      if (menu_pos_y[menu_pos_x] <= 1)
        enable_ac = 1;
      else
        enable_ac = 0;
      break;
    case 4: //style
      bg_tile = bg_tile_addr[menu_pos_y[menu_pos_x]];
      break;
    case 5: //bg color
      bg_pal = menu_pos_y[menu_pos_x] + (menu_pos_y[menu_pos_x]<<2) + (menu_pos_y[menu_pos_x]<<4) + (menu_pos_y[menu_pos_x]<<6);
      break;          
  }
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
  build_menu();
  build_credits();
  generate_rng();
  debug = DEBUG;
  //let's start by the menu
  step_p[0] = SETUP;
  step_p[1] = SETUP;
  step_p_counter[0] = 255;
  step_p_counter[1] = 255;
  //only for menu navigation
  menu_pos_x = 0;
  memset(menu_pos_y,0,sizeof(menu_pos_y));
  actor_x[0][0] = 136;
  actor_x[0][1] = 136;
  actor_x[1][0] = 136;
  actor_x[1][1] = 166;
  actor_x[2][0] = 136;
  actor_x[2][1] = 136;
  actor_y[0][0] = 119;
  actor_y[0][1] = 135;
  actor_y[1][0] = 151;
  actor_y[1][1] = 167;
  actor_y[2][0] = 183;
  actor_y[2][1] = 199;
  check_all_column_list[0] = 0;
  check_all_column_list[1] = 0;
  input_delay_PAD_LEFT[0] = 0; //to prevent multiple inputs
  bg_tile = bg_tile_addr[0];
  bg_pal = 0;
  blind_offset = 0;
  ia = 1;
  speed = 0;
  enable_ac = 1;
  //pointers
  //the address of the boards, will be usefull in fall_board()
  board_address = &boards[0][0][0];
  tmp_boards_address = &tmp_boards[0][0];
  
  //init score and wins at 0
  memset(score,0,sizeof(score));
  memset(wins,0,sizeof(wins));
  memset(ready,0,sizeof(ready));
  memset(ojamas,0,sizeof(ojamas));

  //init sound & music
  apu_init();
  music_ptr = 0;
  play_bayoen();//play only to initialize dmc as on first play the sample doesn't play...
  
  // enable rendering
  ppu_on_all();
  
  while(1) 
  {
    //set sound
    if (!music_ptr && music_selected_ptr != NULL) 
      start_music(music_selected_ptr);//does not loop if not activated

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
      gp_address = &actor_x[0][0];
      gp_address_2  = &actor_y[0][0];
      if (step_p_counter[0] == 255)
      {
        scroll(0,240);//y==240 bottom screen, 0 top
        pad = pad_poll(0);
        if ((pad&PAD_START) || step_p_counter[1] < 255)
        {
          //step_p_counter[1] uses to follow the fade in/out progress
          if (step_p_counter[1] == 255)
            step_p_counter[1] = 0;
          
          build_field();
          if (step_p_counter[1] == 10)
          { 
            step_p_counter[0]=239;
            play_bayoen();
            oam_clear();
          }
          continue;
        }
        else if ((pad&PAD_SELECT) && step_p_counter[1] == 255)
        {
          oam_clear();
          scroll(255,0);
        }
        if ((pad&PAD_DOWN) && menu_pos_x < 5 && input_delay_PAD_LEFT[0] == 0)
        {  
           ++menu_pos_x;
           input_delay_PAD_LEFT[0] = 8;
        }
        if ((pad&PAD_UP) && menu_pos_x > 0 && input_delay_PAD_LEFT[0] == 0)
        {
          --menu_pos_x;
          input_delay_PAD_LEFT[0] = 8;
        }
        if ((pad&PAD_RIGHT) && menu_pos_y[menu_pos_x] < menu_y_step_nb[menu_pos_x] && input_delay_PAD_LEFT[0] == 0)
        {
          ++menu_pos_y[menu_pos_x];
          gp_address[menu_pos_x] += menu_y_step[menu_pos_x];
          input_delay_PAD_LEFT[0] = 8;
          handle_menu_settings();
        }
        if (pad& PAD_LEFT && menu_pos_y[menu_pos_x] > 0 && input_delay_PAD_LEFT[0] == 0)
        {
          --menu_pos_y[menu_pos_x];
          gp_address[menu_pos_x] -=menu_y_step[menu_pos_x];
          input_delay_PAD_LEFT[0] = 8;
          handle_menu_settings();
        }
        // menu's sprites
        // set sprite in OAM buffer, chrnum is tile, attr is attribute, sprid is offset in OAM in bytes
        // returns sprid+4, which is offset for a next sprite
        /*unsigned char __fastcall__ oam_spr(unsigned char x, unsigned char y,
					unsigned char chrnum, unsigned char attr,
					unsigned char sprid);*/
        if (!(pad&PAD_SELECT))
        {
          oam_id = oam_spr(16, actor_y[0][0]+16*menu_pos_x, 0xAE, 0, oam_id ); //vertical menu movement sprite
          for ( i = 0; i < 6; ++i)
            oam_id = oam_spr(gp_address[i], gp_address_2[i], 0xAF, 0, oam_id); //horizontal menu movement sprites
        }
          
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
    
    //to get a 2.5px fall speed 
    ++fall_speed;

    for (current_player = 0 ; current_player < 2 ; ++current_player)
    {
      //current_actor_x points to the current player so we don't have to do a [][] but only a [] which should be faster and avoid some code
      current_actor_x = &actor_x[current_player][0]; //current_actor_x points to the current player x
      current_actor_y = &actor_y[current_player][0]; //current_actor_y points to the current player y
      current_column_height = &column_height[current_player][0];
      current_displayed_pairs = &displayed_pairs[current_player][0];

      if (step_p[current_player] == PLAY && timer_grace_period[current_player] != 255)
      {
        if (timer_grace_period[current_player] != 0)
          handle_controler_and_sprites();
        //we will registrer the number of puyo without something below them
        tmp_counter = 0 ;
        //let's save some compute time and rom space by saving the current column_height of the two pairs in gp_i and gp_j
        //not optimal still have to compute it one more time in the loop below
        //column_height of actor_x[current_player][0]
        gp_i = current_column_height[(current_actor_x[0] >> 4) - pos_x_offset[current_player]];
        //column_height of actor_x[current_player][1]
        gp_j = current_column_height[(current_actor_x[1] >> 4) - pos_x_offset[current_player]];
        column_height_in_ojama[0] = px_2_puyos[gp_i>>4];
        column_height_in_ojama[1] = px_2_puyos[gp_j>>4];
        
        //we need to update the current_actor_y before entering the loop
        //otherwise the test for the offset will fail.
        for (i = 0; i <2; ++i)
        {
          if (actor_dy[current_player][i] != 0) 
          {
            current_actor_y[i] += (actor_dy[current_player][i] + ((timer_grace_period[current_player]!=0 && previous_pad[current_player]&PAD_DOWN)? ((speed) ? 3:(2+(fall_speed&1))) : 0));
          }
        }
        puyo_height_in_ojama[0] = px_2_puyos[current_actor_y[0]>>4];
        puyo_height_in_ojama[1] = px_2_puyos[current_actor_y[1]>>4];
        
        for (i = 0 ; i < 2 ; ++i)
        {
          // puyoseq[0] == red, 1 blue, 2  green, 3 yellow, the good one is taken from
          // puyo_list       p1_puyo_list_index
          // cd update_next for displayed_pairs constructions
          //Debug => P2 is stopped
          if (debug && current_player != 0)
          {
            --current_actor_y[i];
          }
          
          //refresh sprites display
          sprite_addr[current_player][i] = current_displayed_pairs[i] + blind_offset;
          oam_id = oam_meta_spr(current_actor_x[i], current_actor_y[i], oam_id, puyoSeq[sprite_addr[current_player][i]]);

          //test relative to column_height
          column_height_offset = 0;
          if (current_actor_x[0] == current_actor_x[1])
          {
            if (i == 0)
            {
              if (puyo_height_in_ojama[0] > puyo_height_in_ojama[1]) // lowest puyo is 1, so column height artificially raised by 1
                column_height_offset = 1;
            } 
            else
            {
              if (puyo_height_in_ojama[1] > puyo_height_in_ojama[0]) // lowest puyo is 0, so column height artificially raised by 1
                column_height_offset = 1;
            }    
          }
          
          if (
              (column_height_in_ojama[i] + column_height_offset) >= puyo_height_in_ojama[i] ||
              ((column_height_in_ojama[i] + column_height_offset) == (puyo_height_in_ojama[i]-1) && ((current_actor_y[i] & 0x0F) == 0xF)) //test to be able to move after grace_period as started
             )
          {
            tmp_counter += 1 << i; //power of two to know if something is below or not, usefull for fall animation
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
          case 1://either one or two puyos blocked, both are blocked
          case 2:
          case 3:
            if (timer_grace_period[current_player] != 0)
            {
              actor_dy[current_player][0] = 0; 
              actor_dy[current_player][1] = 0;

              if (previous_pad[current_player]&PAD_DOWN)
                timer_grace_period[current_player] = 0;
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

      }
      else
      {
        //we need to move oam_id to not have an offset, should be a better way though...
        //We use the behind to set the sprite behind the background and avoid visual glitch.
        //Note: we should use the oam_meta_sprite_pal to do that, but the oam_id is not followed the same way as with oam_meta_spr...
        //behind is the last element of our metasprite list.
        if (timer_grace_period[current_player] != 255)
        {
          oam_id = oam_meta_spr(0, 0, oam_id, puyoSeq[8]);
          oam_id = oam_meta_spr(0, 0, oam_id, puyoSeq[8]);
        }
      }

      //flush step, that's supposing one opponent has lost
      //we are using tmp_boards, which is slightly larger than boards table.
      //the two extra lines are for storing what will be displayed on the floor
      if (step_p[current_player] == FLUSH)
      {
        //init step, we copy current boards into tmp_boards, and set the floor tiles too
        if (step_p_counter[current_player] == 255)
        {
          //to prevent visual glitch of screen jumping
          step_refresh_ojama_display = 0; 
          cell_address = board_address + (current_player ? 0x4E:0);
          tmp_cell_address = tmp_boards_address;
          for ( i = 0; i < 6; ++i)
          {
            //loop inserted to gain a few bytes of space.
            for (j = 0; j < 13; ++j)
            {
              tmp_cell_address[j] = *cell_address;
              // incrementing by 0x0D in the column loop is unecessary, as basically doing +1 at the end of one column move to the beginning of the next
              ++cell_address;
            }
            // we use 255 as a way to identify a floor tile
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
              build_field();
              break;
            case 2:
               //randomize new pairs
              //generate_rng(); 
              step_p_counter[0] = 3;
              break;
            case 3:
               //print score, hit counter, wins and message
              sprintf(str,"Hit:%2d", nb_hit[0]);
              addr = NTADR_A(2,26);
              vrambuf_put(addr,str,6);
              
              sprintf(str,"%6lu", score[0]);
              addr = NTADR_A(8,27);
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
                  if (pad&PAD_A || ((debug || ia) && i == 1 && ready[0] == 1))
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
              p_puyo_list_index[1] = 0;
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
          //that player lost !
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

        x = (current_actor_x[0]>>4) - pos_x_offset[current_player];
        y = ((current_actor_y[0]>>3)+1)>>1;
        should_destroy[current_player] = (check_board() > 0);

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

          step_p[current_player] = FALL_OJAMA;
          step_p_counter[current_player] = 0;
          
          //add ojama accumulated to the ojama that can fall on opponent board
          /*index for ojama[] for opponent 0=>2; 1=>0*/
          if (current_player == 0)
          {
            ojamas[2] += ojamas[3];
            ojamas[3] = 0;
          }
          else
          {
            ojamas[0] += ojamas[1];
            ojamas[1] = 0;
          }

        }
        continue;
      } else if (step_p[current_player] == CHECK && step_p_counter[current_player] != 0)
      {
        step_p_counter[current_player] = 0;
        continue;
      }

      //after fall (and so destroy) we need to recheck all the board
      //Everything has fallen at that point, so we can go from bottom
      //to top and stop searching as soon empty is found
      //2020/12/14 new idea to improve compute time in some scenario: only check columns that have fallen !
      //with how check_board works, it shouldn't change its result to avoid the untouched columns
      if (step_p[current_player] == CHECK_ALL)
      {
        if (step_p_counter[current_player] < 78)
        { //Start from the left column and go right, do bottom
          //1 column per step to keep some CPU (hopefully)
          i = div_13_lookup[step_p_counter[current_player]];
          if ( (check_all_column_list[current_player] & (1<<i)) > 0)
          {
            //https://embeddedgurus.com/stack-overflow/2011/02/efficient-c-tip-13-use-the-modulus-operator-with-caution/
            //C = A % B is equivalent to C = A – B * (A / B).
            j = 12 - (step_p_counter[current_player] - 13 * (i));
            //if j == 0 not tested as it is the hidden line above screen
            if (j)
            {
              cell_address = board_address + (current_player?0x4E:0) + (i*0xD) + j;
              if (((*cell_address & 7) != EMPTY) && ((*cell_address & FLAG) != FLAG))
              {
                x = i;
                y = j;
                should_destroy[current_player] = (check_board() > 0) || should_destroy[current_player] ;
              }  
            }
          }
          ++step_p_counter[current_player];

          i = div_13_lookup[step_p_counter[current_player]];
          if ( (check_all_column_list[current_player] & (1<<i)) > 0)
          {
            j = 12 - (step_p_counter[current_player] - 13 * (i));
            if (j)
            {
              cell_address = board_address + (current_player?0x4E:0) + (i*0xD) + j;
              if (((*cell_address & 7) != EMPTY) && ((*cell_address & FLAG) != FLAG))
              {
                x = i;
                y = j;
                should_destroy[current_player] = (check_board() > 0) || should_destroy[current_player] ;
              }
            }
          }
          ++step_p_counter[current_player];

          i = div_13_lookup[step_p_counter[current_player]];
          if ( (check_all_column_list[current_player] & (1<<i)) > 0)
          {
            j = 12 - (step_p_counter[current_player] - 13 * (i));
            if (j)
            {
              cell_address = board_address + (current_player?0x4E:0) + (i*0xD) + j;
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
            step_p[current_player] = FALL_OJAMA;
            step_p_counter[current_player] = 0;
            nb_hit[current_player] = 0;// hit combo counter
            //add ojama accumulated to the ojama that can fall on opponent board
            if (current_player == 0)
            {
              ojamas[2] += ojamas[3];
              ojamas[3] = 0;
            }
            else
            {
              ojamas[0] += ojamas[1];
              ojamas[1] = 0;
            }

          }
          //reinit the column marked to be checked
          check_all_column_list[current_player] = 0;
        }
        continue;
      }

      if ( step_p[current_player] == PLAY && timer_grace_period[current_player] == 255 )
      {
        memset(ntbuf1, 0, sizeof(ntbuf1));
        memset(ntbuf2, 0, sizeof(ntbuf2));
        memset(attrbuf, 0, sizeof(attrbuf));
        current_actor_y[0] +=2;
        current_actor_y[1] +=2;
        metatile_y = 0;
        
        if (current_actor_y[0] >= 0x10 && current_actor_y[0] < 0xD0)
        {
          //puyoSeq contains the address of the sprite data, and tile address it at +2 from that
          metatile_ch = *(puyoSeq[sprite_addr[current_player][0]]+0x2);
          set_metatile();
          //attrbuf should take the color for 4 tiles !
          spr_index = (current_player<<1);
          rta_color = return_sprite_color();
          spr_x = current_actor_x[0]>>3;
          spr_y = (current_actor_y[0]>>3);
          attrbuf[0] = return_tile_attribute_color();

          addr = NTADR_A((current_actor_x[0]>>3), (current_actor_y[0]>>3));
          vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
          vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
          vrambuf_put(nt2attraddr(), &attrbuf[0], 1);
        }

        if (current_actor_y[1] >= 0x10 && current_actor_y[1] < 0xD0)
        {
          metatile_ch = *(puyoSeq[sprite_addr[current_player][1]]+0x2);
          set_metatile();
          spr_index = (current_player<<1) + 1;
          rta_color = return_sprite_color();
          spr_x = current_actor_x[1]>>3;
          spr_y = (current_actor_y[1]>>3);
          attrbuf[1] = return_tile_attribute_color( );

          addr = NTADR_A((current_actor_x[1]>>3), (current_actor_y[1]>>3));
          vrambuf_put(addr|VRAMBUF_VERT, ntbuf1, 2);
          vrambuf_put(addr+1|VRAMBUF_VERT, ntbuf2, 2);
          vrambuf_put(nt2attraddr(), &attrbuf[1], 1);
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

//Chopin Waltz Opus 64 N°1
const byte music2[]= {
0x23,0x8e,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x25,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x25,0x87,0x27,0x86,0x28,0x87,0x2a,0x87,0x2c,0x87,0x2d,0x86,0x31,0x95,0x2f,0x86,0x2d,0x87,0x2c,0x87,0x2c,0x87,0x2a,0x86,0x2a,0x87,0x29,0x87,0x2a,0x8d,0x31,0x95,0x2f,0x86,0x2d,0x87,0x2c,0x87,0x2c,0x87,0x2a,0x86,0x29,0x87,0x2a,0x87,0x2c,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x27,0x87,0x25,0x86,0x22,0x87,0x23,0x87,0x25,0x87,0x27,0x86,0x28,0x87,0x2a,0x87,0x2c,0x87,0x2d,0x86,0x31,0x95,0x2f,0x86,0x2d,0x87,0x2c,0x87,0x2c,0x87,0x2a,0x86,0x2a,0x87,0x29,0x87,0x2a,0x8d,0x31,0x95,0x2f,0x86,0x2d,0x87,0x2c,0x87,0x2a,0x87,0x2c,0x86,0x2a,0x87,0x29,0x87,0x2a,0x87,0x2b,0x86,0x2c,0x85,0x2d,0x84,0x2c,0x85,0x2b,0x87,0x2c,0x86,0x2f,0x87,0x2d,0x87,0x2c,0x87,0x2d,0x86,0x2c,0x87,0x2b,0x87,0x2c,0x87,0x31,0x86,0x2f,0x85,0x31,0x84,0x2f,0x85,0x2e,0x87,0x2f,0x86,0x33,0x87,0x31,0x87,0x2f,0x87,0x31,0x86,0x2f,0x87,0x2e,0x87,0x2f,0x87,0x34,0x86,0x33,0x87,0x31,0x87,0x2f,0x87,0x2d,0x86,0x2c,0x87,0x2a,0x87,0x28,0x87,0x27,0x86,0x25,0x87,0x23,0x87,0x21,0x87,0x20,0x86,0x1e,0x87,0x1c,0x87,0x1b,0x87,0x1e,0x86,0x25,0x87,0x23,0x87,0x22,0x87,0x23,0x86,0x25,0x87,0x27,0x87,0x28,0x87,0x2a,0x86,0x2c,0x85,0x2d,0x84,0x2c,0x85,0x2b,0x87,0x2c,0x86,0x2f,0x87,0x2d,0x87,0x2c,0x87,0x2d,0x86,0x2c,0x87,0x2b,0x87,0x2c,0x87,0x31,0x86,0x2f,0x85,0x31,0x84,0x2f,0x85,0x2e,0x87,0x2f,0x86,0x33,0x87,0x31,0x87,0x2f,0x87,0x31,0x86,0x2f,0x87,0x2e,0x87,0x2f,0x87,0x38,0x86,0x36,0x87,0x34,0x87,0x33,0x87,0x31,0x86,0x2f,0x87,0x2d,0x87,0x2c,0x87,0x2a,0x86,0x28,0x87,0x27,0x87,0x25,0x87,0x23,0x86,0x24,0x87,0x27,0x87,0x25,0x87,0x20,0x86,0x21,0x87,0x1b,0x87,0x1c,0x9b,0x2c,0x8d,0x2c,0x85,0x2d,0x84,0x2c,0x85,0x2b,0x87,0x2c,0x86,0x2f,0x87,0x2d,0x87,0x2c,0x87,0x2d,0x86,0x2c,0x87,0x2b,0x87,0x2c,0x87,0x31,0x86,0x2f,0x85,0x31,0x84,0x2f,0x85,0x2e,0x87,0x2f,0x86,0x33,0x87,0x31,0x87,0x2f,0x87,0x31,0x86,0x2f,0x87,0x2e,0x87,0x2f,0x87,0x34,0x86,0x33,0x87,0x31,0x87,0x2f,0x87,0x2d,0x86,0x2c,0x87,0x2a,0x87,0x28,0x87,0x27,0x86,0x25,0x87,0x23,0x87,0x21,0x87,0x20,0x86,0x1e,0x87,0x1c,0x87,0x1b,0x87,0x1e,0x86,0x25,0x87,0x23,0x87,0x22,0x87,0x23,0x86,0x25,0x87,0x27,0x87,0x28,0x87,0x2a,0x86,0x2c,0x85,0x2d,0x84,0x2c,0x85,0x2b,0x87,0x2c,0x86,0x2f,0x87,0x2d,0x87,0x2c,0x87,0x2d,0x86,0x2c,0x87,0x2b,0x87,0x2c,0x87,0x31,0x86,0x2f,0x85,0x31,0x84,0x2f,0x85,0x2e,0x87,0x2f,0x86,0x33,0x87,0x31,0x87,0x2f,0x87,0x31,0x86,0x2f,0x87,0x2e,0x87,0x2f,0x87,0x38,0x86,0x36,0x87,0x34,0x87,0x33,0x87,0x31,0x86,0x2f,0x87,0x2d,0x87,0x2c,0x87,0x2a,0x86,0x28,0x87,0x27,0x87,0x25,0x87,0x23,0x86,0x24,0x87,0x27,0x87,0x25,0x87,0x20,0x86,0x21,0x87,0x1b,0x87,0x1c,0x9b,0x23,0xa8,0x1e,0x8e,0x23,0x9b,0x1f,0x8d,0x23,0x9b,0x20,0x8e,0x2c,0x9b,0x2c,0xa8,0x25,0x8e,0x2c,0x9b,0x27,0x8d,0x2a,0x9b,0x28,0x8e,0x27,0x8a,0x2a,0x8a,0x28,0x8a,0x25,0x8a,0x23,0x9b,0x1e,0x8e,0x23,0x9b,0x1f,0x8d,0x23,0x9b,0x20,0x8e,0x2c,0xa8,0x27,0x8e,0x26,0x8d,0x27,0x8e,0x2f,0x8d,0x25,0x8e,0x2e,0x8d,0x24,0x8e,0x2d,0x8d,0x23,0x8e,0x2c,0x8d,0x20,0x8e,0x25,0x8d,0x23,0x9b,0x1e,0x8e,0x23,0x9b,0x1f,0x8d,0x23,0x9b,0x20,0x8e,0x2c,0x9b,0x2c,0x8d,0x2c,0x9b,0x25,0x8e,0x2c,0x9b,0x27,0x8d,0x2a,0x8e,0x28,0x8d,0x27,0x8e,0x2a,0x8d,0x28,0x95,0x25,0x86,0x23,0x9b,0x1e,0x8e,0x23,0x9b,0x1f,0x8d,0x23,0x9b,0x20,0x8e,0x2c,0xa8,0x2c,0x9b,0x25,0x8e,0x2a,0x9b,0x24,0x8d,0x2a,0x8e,0x23,0x8d,0x29,0x8e,0x2c,0x8d,0x2a,0x8e,0x2f,0x8d,0x23,0xa9,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x25,0x87,0x27,0x87,0x28,0x87,0x2a,0x86,0x2c,0x87,0x2d,0x87,0x31,0x94,0x2f,0x87,0x2d,0x87,0x2c,0x86,0x2c,0x87,0x2a,0x87,0x2a,0x87,0x29,0x86,0x2a,0x8e,0x31,0x94,0x2f,0x87,0x2d,0x87,0x2c,0x86,0x2c,0x87,0x2a,0x87,0x29,0x87,0x2a,0x86,0x2c,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x27,0x87,0x25,0x87,0x22,0x87,0x23,0x86,0x25,0x87,0x27,0x87,0x28,0x87,0x2a,0x86,0x2c,0x87,0x2d,0x87,0x31,0x94,0x2f,0x87,0x2d,0x87,0x2c,0x86,0x2c,0x87,0x2a,0x87,0x2a,0x87,0x29,0x86,0x2a,0x8e,0x31,0x94,0x2f,0x87,0x2d,0x87,0x2c,0x86,0x2a,0x87,0x2c,0x87,0x2a,0x87,0x29,0x86,0x2a,0x87,0x2b,0x87,0x2c,0x84,0x2d,0x85,0x2c,0x84,0x2b,0x87,0x2c,0x87,0x2f,0x87,0x2d,0x86,0x2c,0x87,0x2d,0x87,0x2c,0x87,0x2b,0x86,0x2c,0x87,0x31,0x87,0x2f,0x84,0x31,0x85,0x2f,0x84,0x2e,0x87,0x2f,0x87,0x33,0x87,0x31,0x86,0x2f,0x87,0x31,0x87,0x2f,0x87,0x2e,0x86,0x2f,0x87,0x34,0x87,0x33,0x87,0x31,0x86,0x2f,0x87,0x2d,0x87,0x2c,0x87,0x2a,0x86,0x28,0x87,0x27,0x87,0x25,0x87,0x23,0x86,0x21,0x87,0x20,0x87,0x1e,0x87,0x1c,0x86,0x1b,0x87,0x1e,0x87,0x25,0x87,0x23,0x86,0x22,0x87,0x23,0x87,0x25,0x87,0x27,0x86,0x28,0x87,0x2a,0x87,0x2c,0x84,0x2d,0x85,0x2c,0x84,0x2b,0x87,0x2c,0x87,0x2f,0x87,0x2d,0x86,0x2c,0x87,0x2d,0x87,0x2c,0x87,0x2b,0x86,0x2c,0x87,0x31,0x87,0x2f,0x84,0x31,0x85,0x2f,0x84,0x2e,0x87,0x2f,0x87,0x33,0x87,0x31,0x86,0x2f,0x87,0x31,0x87,0x2f,0x87,0x2e,0x86,0x2f,0x87,0x38,0x87,0x36,0x87,0x34,0x86,0x33,0x87,0x31,0x87,0x2f,0x87,0x2d,0x86,0x2c,0x87,0x2a,0x87,0x28,0x87,0x27,0x86,0x25,0x87,0x23,0x87,0x24,0x87,0x27,0x86,0x25,0x87,0x20,0x87,0x21,0x87,0x1b,0x86,0x1c,0x9b,0x2c,0x92,0x2d,0x85,0x2c,0x84,0x2b,0x87,0x2c,0x87,0x2f,0x87,0x2d,0x86,0x2c,0x87,0x2d,0x87,0x2c,0x87,0x2b,0x86,0x2c,0x87,0x31,0x87,0x2f,0x84,0x31,0x85,0x2f,0x84,0x2e,0x87,0x2f,0x87,0x33,0x87,0x31,0x86,0x2f,0x87,0x31,0x87,0x2f,0x87,0x2e,0x86,0x2f,0x87,0x34,0x87,0x33,0x87,0x31,0x86,0x2f,0x87,0x2d,0x87,0x2c,0x87,0x2a,0x86,0x28,0x87,0x27,0x87,0x25,0x87,0x23,0x86,0x21,0x87,0x20,0x87,0x1e,0x87,0x1c,0x86,0x1b,0x87,0x1e,0x87,0x25,0x87,0x23,0x86,0x22,0x87,0x23,0x87,0x25,0x87,0x27,0x86,0x28,0x87,0x2a,0x87,0x2c,0x84,0x2d,0x85,0x2c,0x84,0x2b,0x87,0x2c,0x87,0x2f,0x87,0x2d,0x86,0x2c,0x87,0x2d,0x87,0x2c,0x87,0x2b,0x86,0x2c,0x87,0x31,0x87,0x2f,0x84,0x31,0x85,0x2f,0x84,0x2e,0x87,0x2f,0x87,0x33,0x87,0x31,0x86,0x2f,0x87,0x31,0x87,0x2f,0x87,0x2e,0x86,0x2f,0x89,0x39,0x87,0x37,0x85,0x35,0x85,0x34,0x85,0x32,0x85,0x30,0x83,0x39,0x85,0x38,0x85,0x36,0x85,0x34,0x86,0x33,0x85,0x31,0x85,0x2f,0x85,0x2d,0x85,0x2c,0x85,0x2a,0x85,0x28,0x85,0x27,0x85,0x25,0x85,0x24,0x85,0x27,0x85,0x25,0x85,0x20,0x85,0x21,0x85,0x1b,0x85,0x1c,0xff
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
//The px must be divided by 16 (0x10) to give the index as C0/0x10 == C  and CF/10 == C too 
//F0 => 13, 00 => 12,  10 =>11, 20 => 10, 30 =>9, 40 => 8, 50 =>7, 60=>6, 70=>5, 80=>4, 90=>3, A0=>2, B0=>1, C0=>0
const byte px_2_puyos[]={12,11,10,9,8,7,6,5,4,3,2,1,0,0,13,13};