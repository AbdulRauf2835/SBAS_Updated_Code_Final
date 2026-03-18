#ifndef STAT_MODEL_143_H
#define STAT_MODEL_143_H

#include "stat_model_types.h"
#include <stdbool.h>

typedef struct {
    StatModel_MT9_Data* data;
    int data_size;
    int data_capacity;
    double* positionDeltas;
    int positionDeltas_size;
    int positionDeltas_capacity;
    double* rateDeltas;
    int rateDeltas_size;
    int rateDeltas_capacity;
    int messageCounter;
    bool initialized;
    StatModel_MT9_Data live_data;
} StatPredictor143;

void stat_model_143_init(StatPredictor143* self);
void stat_model_143_free(StatPredictor143* self);
void stat_model_143_add_training_data(StatPredictor143* self, const StatModel_MT9_Data* mt9_data);
void stat_model_143_analyze(StatPredictor143* self);
const StatModel_MT9_Data* stat_model_143_predict(StatPredictor143* self);

#endif // STAT_MODEL_143_H









