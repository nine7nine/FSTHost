#include <stdlib.h>
#include <stdio.h>
#include <sys/times.h>

static clock_t lastCPU, lastSysCPU, lastUserCPU;
//static int numProcessors;

void CPUusage_init(){
    struct tms timeSample;
    
    lastCPU = times(&timeSample);
    lastSysCPU = timeSample.tms_stime;
    lastUserCPU = timeSample.tms_utime;

/*
    char line[128];
    FILE* file = fopen("/proc/cpuinfo", "r");
    numProcessors = 0;
    while(fgets(line, 128, file) != NULL){
            if (strncmp(line, "processor", 9) == 0) numProcessors++;
    }
    fclose(file);
*/
}

double
CPUusage_getCurrentValue(){
    struct tms timeSample;
    clock_t now;
    double percent;

    now = times(&timeSample);
    if (now <= lastCPU ||
	timeSample.tms_stime < lastSysCPU ||
	timeSample.tms_utime < lastUserCPU)
    {
        //Overflow detection. Just skip this value.
        percent = -1.0;
    } else{
        percent = (timeSample.tms_stime - lastSysCPU) + (timeSample.tms_utime - lastUserCPU);
        percent /= (now - lastCPU);
//        percent /= numProcessors;
	percent *= 100;
    }
    lastCPU = now;
    lastSysCPU = timeSample.tms_stime;
    lastUserCPU = timeSample.tms_utime;

    return percent;
}

