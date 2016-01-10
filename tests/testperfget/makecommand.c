#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *makeCommand(char *command) {
    int totLen = 0,srcLen = 0;
    char head[8];
    char *fullcommand = malloc((totLen = (srcLen = strlen(command))+7+2));    //7 for head, 2 for tail!
    char *cpycommand = malloc(srcLen);
    strcpy(cpycommand, command);
//    printf("totLen is:%d\n", totLen);
    char *cursor = cpycommand;
    while(cursor && *cursor != '\0') {
        if(*cursor=='\t' || *cursor =='\r' || *cursor == ' ' || *cursor == '\b')
            *cursor = '\t';
        cursor++;
    }
    fullcommand[0] = '*';
    fullcommand[1] = '\t';
    fullcommand[2] = ((strlen(command)+7+2)/1000)%10 + '0';
//    printf("%d\n",((strlen(command)+7+2)/1000)%1);
    fullcommand[3] = ((strlen(command)+7+2)/100)%10 + '0';
//    printf("%d\n",((strlen(command)+7+2)/100)%10);
    fullcommand[4] = ((strlen(command)+7+2)/10)%10 + '0';
//    printf("%d\n",((strlen(command)+7+2)/10)%100);
    fullcommand[5] = ((strlen(command)+7+2)/1)%10 + '0';
//    printf("%d\n", ((strlen(command)+7+2)/1)%1000);
    fullcommand[6] = '\t';
    fullcommand[7] = '\0';
    strcat(fullcommand, cpycommand);
    fullcommand[totLen - 2] = '\t';   
    fullcommand[totLen - 1] = '!';
    return fullcommand;
}
