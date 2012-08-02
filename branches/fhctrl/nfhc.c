//      xj-interface.c
//      
//      Shamefully done by blj <blindluke@gmail.com>
//      
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//      

#include <stdio.h>
#include <unistd.h>
#include <cdk/cdk.h>
#include "fhctrl.h"

#define LEFT_MARGIN     2   /* Where the plugin boxes start */
#define RIGHT_MARGIN    80  /* Where the infoboxes start */
#define TOP_MARGIN      7   /* How much space is reserved for the logo */

struct labelbox {
    CDKLABEL    *label;
    char	*text[2];
};


static int get_selector_1(CDKSCREEN *cdkscreen) {
    char    *title  = "<C>Set a new value:";
    char    *label  = "</U/05>Values:";
    CDKITEMLIST *valuelist = 0;
    char    *values[5], *mesg[9];
    int     choice;

    /* Create the choice list. */
    /* *INDENT-EQLS* */
    values[0]      = "<C>value 1";
    values[1]      = "<C>value 2";
    values[2]      = "<C>value 3";
    values[3]      = "<C>value 4";
    values[4]      = "<C>value 5";

    /* Create the itemlist widget. */
    valuelist = newCDKItemlist (
        cdkscreen, CENTER, CENTER,
        title, label, values, 5, 
        0, /* index of the default value */
        TRUE, FALSE
    );

    /* Is the widget null? if so, pass fail code to parent */
    if (valuelist == 0) {
        return 0;
    }

    /* Activate the widget. */
    choice = activateCDKItemlist (valuelist, 0);

    /* Check how they exited from the widget. */
    if (valuelist->exitType == vESCAPE_HIT) {
        mesg[0] = "<C>You hit ESC. No value selected.";
        mesg[1] = "";
        mesg[2] = "<C>Press any key to continue.";
        popupLabel (ScreenOf (valuelist), mesg, 3);
    }
    else if (valuelist->exitType == vNORMAL) {
        mesg[0] = "<C></U>Current selection:";
        mesg[1] = values[choice];
        mesg[2] = "";
        mesg[3] = "<C>Press any key to continue.";
        popupLabel (ScreenOf (valuelist), mesg, 4);
    }

    return (choice + 1);
}

void nfhc(struct Song *song_first, struct Song *song_current, struct FSTPlug **fst) {
    short i, f;
    CDKSCREEN       *cdkscreen;
    CDKLABEL        *app_info, *top_logo, *song_list;
    WINDOW          *screen;
    char            *mesg[9];
    struct labelbox selector[16];
    struct FSTPlug *fp;
    struct FSTState *fs;

    /* Initialize the Cdk screen.  */
    screen = initscr();
    cdkscreen = initCDKScreen (screen);

    /* Start CDK Colors */
    initCDKColor();

    /* top_logo label setup */
    mesg[0] = "</56> ______  ______   ______  __  __   ______   ______   ______  ";
    mesg[1] = "</56>/\\  ___\\/\\  ___\\ /\\__  _\\/\\ \\_\\ \\ /\\  __ \\ /\\  ___\\ /\\__  _\\ ";
    mesg[2] = "</56>\\ \\  __\\\\ \\___  \\\\/_/\\ \\/\\ \\  __ \\\\ \\ \\/\\ \\\\ \\___  \\\\/_/\\ \\/ ";
    mesg[3] = "</56> \\ \\_\\   \\/\\_____\\  \\ \\_\\ \\ \\_\\ \\_\\\\ \\_____\\\\/\\_____\\  \\ \\_\\ ";
    mesg[4] = "</56>  \\/_/    \\/_____/   \\/_/  \\/_/\\/_/ \\/_____/ \\/_____/   \\/_/      proudly done by xj, 2012";
    top_logo = newCDKLabel (cdkscreen, LEFT_MARGIN+10, TOP, mesg, 5, FALSE, FALSE);
    drawCDKLabel (top_logo, TRUE);

    /* app_info label setup */
    mesg[0] = "</U/63>This is the killer VST app by xj(tm).<!05>";
    mesg[1] = "Currently it allows you to:";
    mesg[2] = "<B=+>Change selector no. 1";
    mesg[3] = "<B=+>Do absolutely nothing";
    mesg[4] = "<B=+>Do nothing, but absolutely";
    mesg[5] = "<B=+>Do some other crazy shit, like exit";

    app_info = newCDKLabel (cdkscreen, RIGHT_MARGIN, TOP_MARGIN, mesg, 6, TRUE, TRUE);
    drawCDKLabel (app_info, TRUE);

    /* song_list label setup */
    mesg[0] = "</U/63>Select song preset:<!05>";
    mesg[1] = "<B=+>Song 1 title           (Ctrl+1)    ";
    mesg[2] = "<B=+>Song 2 title           (Ctrl+2)    ";
    mesg[3] = "<B=+>Song 3 title           (Ctrl+3)    ";
    mesg[4] = "<B=+>Song 4 title           (Ctrl+4)    ";
    mesg[5] = "<B=+>Song 5 title           (Ctrl+5)    ";
    mesg[6] = "<B=+>Song 6 title           (Ctrl+6)    ";
    mesg[7] = "<B=+>Song 7 title           (Ctrl+7)    ";
    mesg[8] = "<B=+>Song 8 title           (Ctrl+8)    ";    

    song_list = newCDKLabel (cdkscreen, RIGHT_MARGIN, TOP_MARGIN+10, mesg, 9, TRUE, TRUE);
    drawCDKLabel (song_list, TRUE);

    /* SELECTOR init - same shit for all boxes */

    int lm = 0, tm = 0;
    for (i  = 0; i < 16; i++, tm = tm + 4) {
        if (i == 8) {
           lm = 35;
           tm = 0;
	}

	short j;
        for(j=0; j < 2; j++) {
	   selector[i].text[j] = calloc(1, sizeof(char) * 30);
           memset(selector[i].text[j], '-', 30);
        }

        selector[i].label = newCDKLabel (cdkscreen, LEFT_MARGIN+lm, TOP_MARGIN+(tm), selector[i].text, 2, TRUE, FALSE);
        drawCDKLabel (selector[i].label, TRUE);
    }

    while(true) {
       for (i = f = 0; i < 16; i++) {
          while(f < 128) {
             fp = fst[f++];
             if (!fp) continue;

             fs = song_current->fst_state[fp->id];
             sprintf(selector[i].text[0], "</U/%d>%d %s | CH:%02d | VOL:%02d<!05>", (fs->state ? 59 : 58), 
                fp->id, fp->name, fs->channel, fs->volume);
             sprintf(selector[i].text[1], "#%02d - %-24s", fs->program, fs->program_name);
	     break;
          }

          setCDKLabelMessage(selector[i].label, selector[i].text, 2);
       }
       usleep(300000); // 300ms
    }

//    waitCDKLabel (app_info, ' ');

//    get_selector_1 (cdkscreen);
    
    /* Clean up */
    destroyCDKLabel (app_info);
    for (i = 0; i < 8; i++) {
        destroyCDKLabel (selector[i].label);
    }
    destroyCDKScreen (cdkscreen);
    endCDK();
}
