#include <stdio.h>

#ifndef F_OK
#define F_OK 0
#endif

int
access(
   const char *path,
   int mode
)
{
   FILE *fp;

   if (path == NULL || mode != F_OK)
   {
      return -1;
   }

   fp = fopen(path, "rb");
   if (fp == NULL)
   {
      return -1;
   }
   fclose(fp);
   return 0;
}
