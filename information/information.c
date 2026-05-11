
#include <stdbool.h>

typedef struct 
{
    int voltage_set;
    int voltage_meas;
    int current_set;
    int current_meas;
    bool ocp_on;
    bool out_on;

    int editing_row;
    int editing_col;

    //TODO: findout if col is keept when switching row
    // 

}
information_t;


//TODO: I need to track all the expected changes the ps makes if i do button changes
//      =>  add this tracking change to all the controller functions 
//          
