/*
 * graph.h
 *
 *  Created on: Nov 21, 2019
 *      Author: Matthew Nielsen
 */

#ifndef GRAPH_H_
#define GRAPH_H_

#include <msp430.h>
#include "hal_LCD.h"


int series_a[] = {
                  0x0000, //
                  0x0004, //    |
                  0x4004, //    ||
                  0x4044  //    |||
};

int series_b[] = {
                  0x0000, //
                  0x0008, //    |
                  0x1008, //    ||
                  0x1028, //    |||
};


void clear_int(int position) {
    LCDMEMW[position/2] = 0;
}


void graph_pos(int x_segs, int y_segs, int idx) {
    showInt(x_segs | y_segs, idx);
}


int char_for(unsigned int idx, int series, int position) {
    return (series == 0) ? series_a[idx] : series_b[idx];
}


unsigned int format_percent(float percent, int idx) {
    switch (idx) {
        case 1:
            if (percent <= 0.05) {
                return 0;
            } else if (percent <= 0.11) {
                return 1;
            } else if (percent <= 0.16) {
                return 2;
            } else return 3;
        case 2:
            if (percent <= 0.21) {
                return 0;
            } else if (percent <= 0.26) {
                return 1;
            } else if (percent <= 0.31) {
                return 2;
            } else return 3;
        case 3:
            if (percent <= 0.36) {
                return 0;
            } else if (percent <= 0.41) {
                return 1;
            } else if (percent <= 0.46) {
                return 2;
            } else return 3;
        case 4:
            if (percent <= 0.51 ) {
                return 0;
            } else if (percent <= 0.56) {
                return 1;
            } else if (percent <= 0.61) {
                return 2;
            } else return 3;
        case 5:
            if (percent <= 0.66) {
                return 0;
            } else if (percent <= 0.71) {
                return 1;
            } else if (percent <= 0.76) {
                return 2;
            } else return 3;
        case 6:
            if (percent <= 0.81) {
                return 0;
            } else if (percent <= 0.86) {
                return 1;
            } else if (percent <= 0.91) {
                return 2;
            } else return 3;
        default:
            return 0;
    }
}


void graph(float x_val, float y_val) {
    /* | 0x0400 on the first position to turn on the NEG segment as the origin */
    showInt(char_for(format_percent(x_val, 1), 0, pos1) | char_for(format_percent(y_val, 1), 1, pos1) | 0x0400, pos1);
    showInt(char_for(format_percent(x_val, 2), 0, pos2) | char_for(format_percent(y_val, 2), 1, pos2), pos2);
    showInt(char_for(format_percent(x_val, 3), 0, pos3) | char_for(format_percent(y_val, 3), 1, pos3), pos3);
    showInt(char_for(format_percent(x_val, 4), 0, pos4) | char_for(format_percent(y_val, 4), 1, pos4), pos4);
    showInt(char_for(format_percent(x_val, 5), 0, pos5) | char_for(format_percent(y_val, 5), 1, pos5), pos5);
    showInt(char_for(format_percent(x_val, 6), 0, pos6) | char_for(format_percent(y_val, 6), 1, pos6), pos6);
}


#endif /* GRAPH_H_ */