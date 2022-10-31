#define _BSD_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int
mkstemp(char *template)
{
   char *s;
   int i, fd;

   s = strdup(template);
   if(s == NULL)
       return -1;
   for(i=0; i<20; i++){
       strcpy(s, template);
       mktemp(s);
       if((fd = open(s, O_RDWR | O_CREAT | O_EXCL, 0600)) >= 0){
           strcpy(template, s);
           free(s);
           return fd;
       }
   }
   free(s);
   return -1;
}
