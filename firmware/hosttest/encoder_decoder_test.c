/* Regression: the firmware must not drop detents across idle/bounce. */
#include <stdint.h>
#include <stdio.h>
#include "../app/board/fm1_encoder_decoder.h"
static const int8_t quad[16]={0,1,-1,0, -1,0,0,1, 1,0,0,-1, 0,-1,1,0};
static uint8_t state=255;
static int8_t part=0;
static int move(unsigned phase){return fm1_encoder_decode(&state,&part,phase,quad);}
static void reset(unsigned p){state=255;part=0;move(p);}
static int failures=0;
#define CHECK(ok,text) do{if(!(ok)){printf("FAIL %s\n",text);failures++;}else printf("PASS %s\n",text);}while(0)
int main(void){
 reset(0);
 CHECK(move(1)==0 && move(3)==0,"first half turn retained");
 int idle=0;for(int i=0;i<2000;i++)idle+=move(3);
 CHECK(idle==0 && part==2,"long idle neither emits nor resets partial turn");
 CHECK(move(2)==0 && move(0)==1,"complete half-turn after pause triggers detent");
 CHECK(move(2)==0 && move(3)==0 && move(1)==0 && move(0)==-1,"reverse detent");
 reset(0);
 CHECK(move(1)==0 && move(0)==0 && part==0,"bounce cancels");
 CHECK(move(3)==0 && part==0,"diagonal transition rejected");
 CHECK(move(2)==0 && move(0)==0 && move(1)==0 && move(3)==1,"resynchronize after diagonal");
 reset(0);
 int total=0;for(int i=0;i<250;i++){total+=move(1);total+=move(3);total+=move(2);total+=move(0);}
 CHECK(total==250,"250 detents without freeze");
 return failures?1:0;
}
