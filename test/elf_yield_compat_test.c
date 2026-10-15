#include <assert.h>
#include <limits.h>
#include <stdio.h>
static unsigned traces, switches;
void yield_test_trace(void) { ++traces; }
void _frxt_setup_switch(void) { assert(traces==switches+1); ++switches; }
void vPortEvaluateYieldFromISR(int argc, ...);
int main(void) {
    vPortEvaluateYieldFromISR(0);
    assert(switches==1 && traces==1);
    vPortEvaluateYieldFromISR(1, 1);
    assert(switches==2 && traces==2);
    const int values[]={0,-1,2,INT_MIN,INT_MAX};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)
        vPortEvaluateYieldFromISR(1,values[i]);
    assert(switches==2 && traces==2);
    // Legacy implementation treats every nonzero argc as one int argument.
    vPortEvaluateYieldFromISR(-1,1);
    vPortEvaluateYieldFromISR(2,1,0);
    assert(switches==4 && traces==4);
    puts("ELF legacy yield bridge exact integer semantics PASS");
}
