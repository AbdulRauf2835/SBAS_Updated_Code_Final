#include "stat_model_143.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int add_sbas_data_143(StatPredictor143* self, const StatModel_MT9_Data* entry) {
    if (self->data_size >= self->data_capacity) {
        int new_cap = (self->data_capacity == 0) ? 16 : self->data_capacity * 2;
        StatModel_MT9_Data* new_data = (StatModel_MT9_Data*)realloc(self->data, sizeof(StatModel_MT9_Data) * new_cap);
        if (!new_data) return -1;
        self->data = new_data;
        self->data_capacity = new_cap;
    }
    self->data[self->data_size++] = *entry;
    return 0;
}

static int double_array_add(double** arr, int* size, int* capacity, double value) {
    if (*size >= *capacity) {
        int new_cap = (*capacity == 0) ? 8 : *capacity * 2;
        double* new_arr = (double*)realloc(*arr, sizeof(double) * new_cap);
        if (!new_arr) return -1;
        *arr = new_arr;
        *capacity = new_cap;
    }
    (*arr)[*size] = value;
    (*size)++;
    return 0;
}

static int add_delta(double** arr, int* size, int* cap, double v1, double v2, double v3) {
    if (double_array_add(arr, size, cap, v1) != 0) return -1;
    if (double_array_add(arr, size, cap, v2) != 0) return -1;
    if (double_array_add(arr, size, cap, v3) != 0) return -1;
    return 0;
}

void stat_model_143_init(StatPredictor143* self) {
    memset(self, 0, sizeof(StatPredictor143));
    self->initialized = false;
}

void stat_model_143_free(StatPredictor143* self) {
    free(self->data);
    free(self->positionDeltas);
    free(self->rateDeltas);
    stat_model_143_init(self);
}

void stat_model_143_add_training_data(StatPredictor143* self, const StatModel_MT9_Data* mt9_data) {
    add_sbas_data_143(self, mt9_data);
}

void stat_model_143_analyze(StatPredictor143* self) {
    if (self->data_size < 2) {
        self->initialized = false;
        return;
    }
    printf("STAT_MODEL_143: Analyzing movement pattern...\n");

    free(self->positionDeltas);
    free(self->rateDeltas);
    self->positionDeltas = NULL;
    self->rateDeltas = NULL;
    self->positionDeltas_size = 0;
    self->rateDeltas_size = 0;
    self->positionDeltas_capacity = 0;
    self->rateDeltas_capacity = 0;

    for (int i = 1; i < self->data_size; i++) {
        double dX = self->data[i].xg_m - self->data[i-1].xg_m;
        double dY = self->data[i].yg_m - self->data[i-1].yg_m;
        double dZ = self->data[i].zg_m - self->data[i-1].zg_m;
        add_delta(&self->positionDeltas, &self->positionDeltas_size, &self->positionDeltas_capacity, dX, dY, dZ);

        double dVx = self->data[i].xg_rate - self->data[i-1].xg_rate;
        double dVy = self->data[i].yg_rate - self->data[i-1].yg_rate;
        double dVz = self->data[i].zg_rate - self->data[i-1].zg_rate;
        add_delta(&self->rateDeltas, &self->rateDeltas_size, &self->rateDeltas_capacity, dVx, dVy, dVz);
    }
    
    if (self->data_size > 0) {
        self->live_data = self->data[self->data_size - 1];
        self->initialized = true;
    }
    printf("STAT_MODEL_143: Analysis complete. Initialized: %d\n", self->initialized);
}


static void predict_next_pos_143(StatPredictor143* self, StatModel_MT9_Data* data) {
    int n = self->positionDeltas_size / 3;
    if (n < 4) return; 

    double* p = self->positionDeltas;
    double trend1X = p[(n-1)*3+0] - p[(n-2)*3+0];
    double trend2X = p[(n-2)*3+0] - p[(n-3)*3+0];
    double trend3X = p[(n-3)*3+0] - p[(n-4)*3+0];
    double avgTrendX = (trend1X + trend2X + trend3X) / 3.0;
    double nextDeltaX = p[(n-1)*3+0] + avgTrendX;

    double trend1Y = p[(n-1)*3+1] - p[(n-2)*3+1];
    double trend2Y = p[(n-2)*3+1] - p[(n-3)*3+1];
    double trend3Y = p[(n-3)*3+1] - p[(n-4)*3+1];
    double avgTrendY = (trend1Y + trend2Y + trend3Y) / 3.0;
    double nextDeltaY = p[(n-1)*3+1] + avgTrendY;

    double trend1Z = p[(n-1)*3+2] - p[(n-2)*3+2];
    double trend2Z = p[(n-2)*3+2] - p[(n-3)*3+2];
    double trend3Z = p[(n-3)*3+2] - p[(n-4)*3+2];
    double avgTrendZ = (trend1Z + trend2Z + trend3Z) / 3.0;
    double nextDeltaZ = p[(n-1)*3+2] + avgTrendZ;

    data->xg_m += nextDeltaX;
    data->yg_m += nextDeltaY;
    data->zg_m += nextDeltaZ;

    add_delta(&self->positionDeltas, &self->positionDeltas_size, &self->positionDeltas_capacity, nextDeltaX, nextDeltaY, nextDeltaZ);
}

static void predict_next_vel_143(StatPredictor143* self, StatModel_MT9_Data* data) {
    int n = self->rateDeltas_size / 3;
    if (n < 4) return;

    double* r = self->rateDeltas;
    double trend1Vx = r[(n-1)*3+0] - r[(n-2)*3+0];
    double trend2Vx = r[(n-2)*3+0] - r[(n-3)*3+0];
    double trend3Vx = r[(n-3)*3+0] - r[(n-4)*3+0];
    double avgTrendVx = (trend1Vx + trend2Vx + trend3Vx) / 3.0;
    double nextDeltaVx = r[(n-1)*3+0] + avgTrendVx;

    double trend1Vy = r[(n-1)*3+1] - r[(n-2)*3+1];
    double trend2Vy = r[(n-2)*3+1] - r[(n-3)*3+1];
    double trend3Vy = r[(n-3)*3+1] - r[(n-4)*3+1];
    double avgTrendVy = (trend1Vy + trend2Vy + trend3Vy) / 3.0;
    double nextDeltaVy = r[(n-1)*3+1] + avgTrendVy;

    double trend1Vz = r[(n-1)*3+2] - r[(n-2)*3+2];
    double trend2Vz = r[(n-2)*3+2] - r[(n-3)*3+2];
    double trend3Vz = r[(n-3)*3+2] - r[(n-4)*3+2];
    double avgTrendVz = (trend1Vz + trend2Vz + trend3Vz) / 3.0;
    double nextDeltaVz = r[(n-1)*3+2] + avgTrendVz;

    data->xg_rate += nextDeltaVx;
    data->yg_rate += nextDeltaVy;
    data->zg_rate += nextDeltaVz;
    
    add_delta(&self->rateDeltas, &self->rateDeltas_size, &self->rateDeltas_capacity, nextDeltaVx, nextDeltaVy, nextDeltaVz);
}

static void predict_next_acc_143(StatPredictor143* self, StatModel_MT9_Data* data) {
    self->messageCounter++;
    if (self->messageCounter % 2 == 0) {
        data->zg_accel -= 0.000006;
    }
    if (self->messageCounter % 6 == 0) {
        data->xg_accel -= 0.000012;
        data->yg_accel -= 0.000013;
    }
}

const StatModel_MT9_Data* stat_model_143_predict(StatPredictor143* self) {
    // Predicts in-place, modifying the live_data
    predict_next_pos_143(self, &self->live_data);
    predict_next_vel_143(self, &self->live_data);
    predict_next_acc_143(self, &self->live_data);
    return &self->live_data;
}