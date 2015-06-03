/**
 * $Id: worker.c 881 2013-12-16 05:37:34Z rp_jmenart $
 *
 * @brief Red Pitaya Spectrum Analyzer worker.
 *
 * @Author Jure Menart <juremenart@gmail.com>
 *         
 * (c) Red Pitaya  http://www.redpitaya.com
 *
 * This part of code is written in C programming language.
 * Please visit http://en.wikipedia.org/wiki/C_(programming_language)
 * for more details on the language used herein.
 */

#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "spectrometerApp.h"
#include "common.h"
#include "../../rpbase/src/common.h"
#include "../../rpbase/src/spec_fpga.h"
#include "../../rpbase/src/spec_dsp.h"

const wf_func_table_t* wf_func_table;

/* JPG outputs: c_jpg_file_path+[1|2]+_+jpg_cnt(3 digits)+c_jpg_file_suf */
const char c_jpg_dir_path[]="/tmp";
const char c_jpg_file_name[]="wat";
const char c_jpg_file_path[]="/tmp/wat";
const char c_jpg_file_suf[]=".jpg";
const int  c_jpg_max_file  = 63;
const int  c_save_jpg_cnt  = 10; /* Repetition how often the JPG is stored */
char      *jpg_fname_cha = NULL;
char      *jpg_fname_chb = NULL;

pthread_t *rp_spectr_thread_handler = NULL;
void *rp_spectr_worker_thread(void *args);

/* Signals directly pointing at the FPGA mem space */
int                  *rp_fpga_cha_signal, *rp_fpga_chb_signal;

/* Internal structures */
/* Size = SPECTR_FPGA_SIG_LEN  */
double *rp_cha_in = NULL;
double *rp_chb_in = NULL;

/* DSP structures */
/* size = c_dsp_sig_len */
double *rp_cha_fft = NULL;
double *rp_chb_fft = NULL;

/* Output 3 x SPECTR_OUT_SIG signals - used internally for calculation */
float               **rp_tmp_signals = NULL;

/* Parameters & signals communicating with 'external world' */
pthread_mutex_t       rp_spectr_ctrl_mutex = PTHREAD_MUTEX_INITIALIZER;
rp_spectr_worker_state_t rp_spectr_ctrl;
rp_app_params_t       rp_spectr_params[PARAMS_NUM];
int                   rp_spectr_params_dirty;
int                   rp_spectr_params_fpga_update;

pthread_mutex_t        rp_spectr_sig_mutex = PTHREAD_MUTEX_INITIALIZER;
float                **rp_spectr_signals = NULL;
rp_spectr_worker_res_t rp_spectr_result;
int                    rp_spectr_signals_dirty = 0;

int rp_spectr_worker_init(const wf_func_table_t* wf_f)
{
	wf_func_table = wf_f;

    int ret_val;

    rp_spectr_ctrl               = rp_spectr_idle_state;
    rp_spectr_params_dirty       = 1;
    rp_spectr_params_fpga_update = 1;

    rp_spectr_clean_tmpdir(c_jpg_dir_path);

    rp_cleanup_signals(&rp_spectr_signals);
    if(rp_create_signals(&rp_spectr_signals) < 0)
        return -1;

    rp_cleanup_signals(&rp_tmp_signals);

    if(rp_create_signals(&rp_tmp_signals) < 0) {
        rp_cleanup_signals(&rp_spectr_signals);
        return -1;
    }

    rp_cha_in = (double *)malloc(sizeof(double) * SPECTR_FPGA_SIG_LEN);
    rp_chb_in = (double *)malloc(sizeof(double) * SPECTR_FPGA_SIG_LEN);
    rp_cha_fft = (double *)malloc(sizeof(double) * c_dsp_sig_len);
    rp_chb_fft = (double *)malloc(sizeof(double) * c_dsp_sig_len);
    if(!rp_cha_in || !rp_chb_in || !rp_cha_fft || !rp_chb_fft) {
        rp_spectr_worker_clean();
        return -1;
    }

    jpg_fname_cha = 
        (char *)malloc((strlen(c_jpg_file_path)+strlen(c_jpg_file_suf)+5+1));
    jpg_fname_chb = 
        (char *)malloc((strlen(c_jpg_file_path)+strlen(c_jpg_file_suf)+5+1));
    if(!jpg_fname_cha || !jpg_fname_chb) {
        rp_spectr_worker_clean();
        return -1;
    }

    if(spectr_fpga_init() < 0) {
        rp_spectr_worker_clean();
        return -1;
    }

    if(rp_spectr_hann_init() < 0) {
        rp_spectr_worker_clean();
        return -1;
    }

    if(rp_spectr_fft_init() < 0) {
        rp_spectr_worker_clean();
        return -1;
    }

    if(wf_func_table->rp_spectr_wf_init() < 0) {
        rp_spectr_worker_clean();
        return -1;
    }

    spectr_fpga_get_sig_ptr(&rp_fpga_cha_signal, &rp_fpga_chb_signal);

    rp_spectr_thread_handler = (pthread_t *)malloc(sizeof(pthread_t));
    if(rp_spectr_thread_handler == NULL) {
        rp_cleanup_signals(&rp_spectr_signals);
        rp_cleanup_signals(&rp_tmp_signals);
        return -1;
    }

    ret_val = 
        pthread_create(rp_spectr_thread_handler, NULL, 
                       rp_spectr_worker_thread, NULL);
    if(ret_val != 0) {
        spectr_fpga_exit();

        rp_cleanup_signals(&rp_spectr_signals);
        rp_cleanup_signals(&rp_tmp_signals);
        fprintf(stderr, "pthread_create() failed: %s\n", 
                strerror(errno));
        return -1;
    }

    return 0;
}

int rp_spectr_worker_clean(void)
{
    spectr_fpga_exit();
    rp_cleanup_signals(&rp_spectr_signals);
    rp_cleanup_signals(&rp_tmp_signals);
    rp_spectr_hann_clean();
    rp_spectr_fft_clean();
    wf_func_table->rp_spectr_wf_clean();

    if(jpg_fname_cha) {
        free(jpg_fname_cha);
        jpg_fname_cha = NULL;
    }
    if(jpg_fname_chb) {
        free(jpg_fname_chb);
        jpg_fname_chb = NULL;
    }
    if(rp_cha_in) {
        free(rp_cha_in);
        rp_cha_in = NULL;
    }
    if(rp_chb_in) {
        free(rp_chb_in);
        rp_chb_in = NULL;
    }
    if(rp_cha_fft) {
        free(rp_cha_fft);
        rp_cha_fft = NULL;
    }
    if(rp_chb_fft) {
        free(rp_chb_fft);
        rp_chb_fft = NULL;
    }

    return 0;
}

int rp_spectr_worker_exit(void)
{
    int ret_val = 0;

    rp_spectr_worker_change_state(rp_spectr_quit_state);
    if(rp_spectr_thread_handler) {
        ret_val = pthread_join(*rp_spectr_thread_handler, NULL);
        free(rp_spectr_thread_handler);
        rp_spectr_thread_handler = NULL;
    }
    if(ret_val != 0) {
        fprintf(stderr, "pthread_join() failed: %s\n", 
                strerror(errno));
    }
    rp_spectr_worker_clean();
    rp_spectr_clean_tmpdir(c_jpg_dir_path);
    return 0;
}

int rp_spectr_worker_change_state(rp_spectr_worker_state_t new_state)
{
    if(new_state >= rp_spectr_nonexisting_state)
        return -1;
    pthread_mutex_lock(&rp_spectr_ctrl_mutex);
    rp_spectr_ctrl = new_state;
    pthread_mutex_unlock(&rp_spectr_ctrl_mutex);
    return 0;
}

int rp_spectr_worker_update_params(rp_app_params_t *params, int fpga_update)
{
    pthread_mutex_lock(&rp_spectr_ctrl_mutex);
    memcpy(&rp_spectr_params, params, sizeof(rp_app_params_t)*PARAMS_NUM);
    rp_spectr_params_dirty       = 1;
    rp_spectr_params_fpga_update = fpga_update;
    pthread_mutex_unlock(&rp_spectr_ctrl_mutex);
    return 0;
}

int rp_spectr_worker_update_params_by_idx(int value, size_t idx, int fpga_update)
{
    pthread_mutex_lock(&rp_spectr_ctrl_mutex);


	fprintf(stderr, "rp_spectr_worker_update_params_by_idx = %d %d\n", idx, value);
    rp_spectr_params[idx].value = value;
    rp_spectr_params_dirty       = 1;
    rp_spectr_params_fpga_update = fpga_update;

    pthread_mutex_unlock(&rp_spectr_ctrl_mutex);
    return 0;
}

int rp_spectr_clean_signals(void)
{
    pthread_mutex_lock(&rp_spectr_sig_mutex);
    rp_spectr_signals_dirty = 0;
    pthread_mutex_unlock(&rp_spectr_sig_mutex);
    return 0;
}

int rp_spectr_clean_tmpdir(const char *dir)
{
    DIR *dp;
    struct dirent *ep;
    int fname_len = strlen(c_jpg_file_name);
    int fsuf_len = strlen(c_jpg_file_suf);
    int ret = 0;

    if((dp = opendir(dir)) == NULL) {
        fprintf(stderr, "%s() opendir() failed: %s\n",
                __FUNCTION__, strerror(errno));
        return -1;
    }

    while((ep = readdir(dp))) {
        const char *filename = ep->d_name;
        int f_len = strlen(filename);

        if((f_len > fname_len) &&
                (strncmp(filename, c_jpg_file_name, fname_len) == 0) &&
                (strncmp(&filename[f_len-fsuf_len], c_jpg_file_suf, fsuf_len) == 0)) {

            int full_name_len = strlen(dir) + strlen(filename) + 2;
            char *full_name;
            full_name = (char *)malloc(full_name_len * sizeof(char));
            if(full_name == NULL) {
                fprintf(stderr, "%s() malloc() failed: %s\n",
                        __FUNCTION__, strerror(errno));
                ret = -1;
                break;
            }
            sprintf(full_name, "%s/%s", dir, filename);
            full_name[full_name_len-1]='\0';
            if(unlink((const char *)full_name) < 0) {
                fprintf(stderr, "%s() unlink() for '%s' failed: %s\n",
                        __FUNCTION__, full_name, strerror(errno));
                /* still continue erasing */
                ret = -1;
            }

            free(full_name);
        }
    }

    closedir(dp);

    return ret;
}

int rp_spectr_get_signals_channel(float** signals, size_t size)
{
    pthread_mutex_lock(&rp_spectr_sig_mutex);
    if(rp_spectr_signals_dirty == 0) {
        pthread_mutex_unlock(&rp_spectr_sig_mutex);
        return -1;
    }

	size_t i;
	for (i = 0; i < 3; ++i)
		memcpy(signals[i], rp_spectr_signals[i], sizeof(float)*size);

    rp_spectr_signals_dirty = 0;

    pthread_mutex_unlock(&rp_spectr_sig_mutex);
    return 0;
}

int rp_spectr_get_params(rp_spectr_worker_res_t *result)
{
    pthread_mutex_lock(&rp_spectr_sig_mutex);
    if(rp_spectr_signals_dirty == 0) {
        pthread_mutex_unlock(&rp_spectr_sig_mutex);
        return -1;
    }

    result->jpg_idx          = rp_spectr_result.jpg_idx;
    result->peak_pw_cha      = rp_spectr_result.peak_pw_cha;
    result->peak_pw_freq_cha = rp_spectr_result.peak_pw_freq_cha;
    result->peak_pw_chb      = rp_spectr_result.peak_pw_chb;
    result->peak_pw_freq_chb = rp_spectr_result.peak_pw_freq_chb;

    pthread_mutex_unlock(&rp_spectr_sig_mutex);
    return 0;
}

int rp_spectr_set_signals(float **source, rp_spectr_worker_res_t result)
{
    pthread_mutex_lock(&rp_spectr_sig_mutex);
    memcpy(rp_spectr_signals[0], source[0], sizeof(float)*SPECTR_OUT_SIG_LEN);
    memcpy(rp_spectr_signals[1], source[1], sizeof(float)*SPECTR_OUT_SIG_LEN);
    memcpy(rp_spectr_signals[2], source[2], sizeof(float)*SPECTR_OUT_SIG_LEN);

    rp_spectr_signals_dirty = 1;

    rp_spectr_result.jpg_idx          = result.jpg_idx;
    rp_spectr_result.peak_pw_cha      = result.peak_pw_cha;
    rp_spectr_result.peak_pw_freq_cha = result.peak_pw_freq_cha;
    rp_spectr_result.peak_pw_chb      = result.peak_pw_chb;
    rp_spectr_result.peak_pw_freq_chb = result.peak_pw_freq_chb;

    pthread_mutex_unlock(&rp_spectr_sig_mutex);

    return 0;
}

void *rp_spectr_worker_thread(void *args)
{
    rp_spectr_worker_state_t old_state, state;
    rp_app_params_t          curr_params[PARAMS_NUM];
    int                      fpga_update = 1;
    int                      params_dirty = 1;
    int                      loop_cnt = 0; /* each N save jpeg */
    int                      jpg_fn_cnt = 0;
    /* depends on freq_range - do not save too much or too less */
    int                      jpg_write_div = 10;
    rp_spectr_worker_res_t   tmp_result;

    pthread_mutex_lock(&rp_spectr_ctrl_mutex);
    old_state = state = rp_spectr_ctrl;
    pthread_mutex_unlock(&rp_spectr_ctrl_mutex);

    while(1) {
        /* update states - we save also old state to see if we need to reset
         * FPGA 
         */
        old_state = state;
        pthread_mutex_lock(&rp_spectr_ctrl_mutex);
        state = rp_spectr_ctrl;
        if(rp_spectr_params_dirty) {			
            memcpy(&curr_params, &rp_spectr_params, sizeof(rp_app_params_t)*PARAMS_NUM);
			fprintf(stderr, "dirty curr_params = %f rp_spectr_params = %f\n", curr_params[FREQ_RANGE_PARAM].value, rp_spectr_params[FREQ_RANGE_PARAM].value);
            fpga_update = rp_spectr_params_fpga_update;
            rp_spectr_params_dirty = 0;
        }
        pthread_mutex_unlock(&rp_spectr_ctrl_mutex);

        /* request to stop worker thread, we will shut down */
        if(state == rp_spectr_quit_state) {
            return 0;
        }

        if(fpga_update) {
			fprintf(stderr, "update\n");
            spectr_fpga_reset();
            if(spectr_fpga_update_params(0, 0, 0, 0, 0, (int)curr_params[FREQ_RANGE_PARAM].value, curr_params[EN_AVG_AT_DEC].value) < 0) {
				rp_spectr_worker_change_state(rp_spectr_auto_state);
				fprintf(stderr, "updated\n");
            }

            fpga_update = 0;
            wf_func_table->rp_spectr_wf_clean_map();
            switch((int)curr_params[FREQ_RANGE_PARAM].value) {
            case 0:
            case 1:
                jpg_write_div = 10;
                break;
            case 2:
            case 3:
                jpg_write_div = 5;
                break;
            case 4:
            case 5:
                jpg_write_div = 0;
                break;
            }
        }

        if(state == rp_spectr_idle_state) {
            usleep(10000);
            continue;
        }

        /* Start the writting machine */
        spectr_fpga_arm_trigger();
        
        usleep(10);

        spectr_fpga_set_trigger(1);

        /* start working */
        pthread_mutex_lock(&rp_spectr_ctrl_mutex);
        old_state = state = rp_spectr_ctrl;
        pthread_mutex_unlock(&rp_spectr_ctrl_mutex);
        if((state == rp_spectr_idle_state) || (state == rp_spectr_abort_state)) {
            continue;
        } else if(state == rp_spectr_quit_state) {
            break;
        }

        /* polling until data is ready */
        while(1) {
            pthread_mutex_lock(&rp_spectr_ctrl_mutex);
            state = rp_spectr_ctrl;
            params_dirty = rp_spectr_params_dirty;
            pthread_mutex_unlock(&rp_spectr_ctrl_mutex);
            /* change in state, abort polling */
            if((state != old_state) || params_dirty) {
                break;
            }
                
            if(spectr_fpga_triggered()) {
                break;
            }
        }

        if((state != old_state) || params_dirty) {
            params_dirty = 0;
            continue;
        }

        /* retrieve data and process it*/
        spectr_fpga_get_signal(&rp_cha_in, &rp_chb_in);

        rp_spectr_prepare_freq_vector(&rp_tmp_signals[0], 
                                      get_spectr_fpga_smpl_freq(),
                                      curr_params[FREQ_RANGE_PARAM].value);

        rp_spectr_hann_filter(&rp_cha_in[0], &rp_chb_in[0],
                              &rp_cha_in, &rp_chb_in);
        
        rp_spectr_fft(&rp_cha_in[0], &rp_chb_in[0], 
                      (double **)&rp_cha_fft, (double **)&rp_chb_fft);
        
        rp_spectr_decimate(&rp_cha_fft[0], &rp_chb_fft[0], 
                           (float **)&rp_tmp_signals[1], 
                           (float **)&rp_tmp_signals[2],
                           c_dsp_sig_len, SPECTR_OUT_SIG_LEN);
        
        rp_spectr_cnv_to_dBm(&rp_tmp_signals[1][0], &rp_tmp_signals[2][0], 
                             (float **)&rp_tmp_signals[1], 
                             (float **)&rp_tmp_signals[2], 
                             &tmp_result.peak_pw_cha, 
                             &tmp_result.peak_pw_freq_cha,
                             &tmp_result.peak_pw_chb, 
                             &tmp_result.peak_pw_freq_chb,
                             curr_params[FREQ_RANGE_PARAM].value);

        /* Calculate the map used for Waterfall diagram  */
        wf_func_table->rp_spectr_wf_calc(&rp_cha_fft[0], &rp_chb_fft[0]);

        if((jpg_write_div == 0) || (loop_cnt++%jpg_write_div == 0)) {
            jpg_fn_cnt++;
            if(jpg_fn_cnt > c_jpg_max_file) 
                jpg_fn_cnt = 0;

            sprintf(jpg_fname_cha, "%s%01d_%03d%s", c_jpg_file_path, 
                    1, jpg_fn_cnt, c_jpg_file_suf);
            sprintf(jpg_fname_chb, "%s%01d_%03d%s", c_jpg_file_path, 
                    2, jpg_fn_cnt, c_jpg_file_suf);
            wf_func_table->rp_spectr_wf_save_jpeg(jpg_fname_cha, jpg_fname_chb);

            /* Report index back to the user */
        } else if(loop_cnt > 1e6) {
            loop_cnt = 0;
        }


        /* Copy the result to the output part - and also the index of
         * last JPEG file index */
        tmp_result.jpg_idx = jpg_fn_cnt;
        rp_spectr_set_signals(rp_tmp_signals, tmp_result);

        usleep(10000);
    }

    return 0;
}

// API


/* Describe app. parameters with some info/limitations */
rp_app_params_t rp_main_params[PARAMS_NUM+1] = {
    { /* xmin - currently ignored */
        "xmin", -1000000, 0, 1, -10000000, +10000000 },
    { /* xmax - currently ignored   */
        "xmax", +63, 0, 1, -10000000, +10000000 },
    { /* freq_range::
       *    0 - 62.5 [MHz]
       *    1 - 7.8 [MHz]
       *    2 - 976 [kHz]
       *    3 - 61 [kHz]
       *    4 - 7.6 [kHz]
       *    5 - 953 [Hz] */
        "freq_range", 0, 1, 0,         0,         5 },
    { /* freq_unit:
       *    0 - [Hz]
       *    1 - [kHz]
       *    2 - [MHz]   */
        "freq_unit",  2, 0, 1,         0,         2 },
    { /* peak1_freq */
        "peak1_freq", 0, 0, 1,         0,         +1e6 },
    { /* peak1_power    */
        "peak1_power", 0, 0, 1, -10000000, +10000000 },
    { /* peak1_unit - same enumeration as freq_unit */
        "peak1_unit", 0, 0, 1,       0,       2 },
    { /* peak2_freq */
        "peak2_freq", 0, 0, 1,         0,         1e6 },
    { /* time_range */
        "peak2_power", 0, 0, 1,         -1e7, 1e7 },
    { /* peak2_unit - same enumeration as freq_unit */
        "peak2_unit", 0, 0, 1,         0,         2 },
    { /* JPG Waterfall files index */
        "w_idx", 0, 0, 1, 0, 1000 },
    { /* en_avg_at_dec:
           *    0 - disable
           *    1 - enable */
        "en_avg_at_dec", 1, 0, 1,      0,         1 },
    { /* Must be last! */
        NULL, 0.0, -1, -1, 0.0, 0.0 }
};


void SpecIntervalInit();

int spec_run(const wf_func_table_t* wf_f)
{
    fprintf(stderr, "SPEC RUN Loading spectrum version %s-%s.\n", "1" ,"1");

    if(rp_spectr_worker_init(wf_f) < 0) {
	    fprintf(stderr, "rp_spectr_worker_init failed\n");
        return -1;
    }

    rp_set_params(rp_main_params, PARAMS_NUM);

    rp_spectr_worker_change_state(rp_spectr_auto_state);

	SpecIntervalInit();

    return 0;
}

int spec_stop()
{
    fprintf(stderr, "Unloading spectrum version %s-%s.\n", "1", "1");

    rp_spectr_worker_exit();

    return 0;
}

int spec_getViewData(float **signals, size_t size)
{
    return rp_spectr_get_signals_channel(signals, size);
}

int spec_getJpgIdx(int* jpg)
{
	rp_spectr_worker_res_t res;
	int ret = rp_spectr_get_params(&res);
	if (!ret)
	{
		*jpg = res.jpg_idx;
	}

	return ret;
}

int spec_getPeakPower(int channel, float* power)
{
	rp_spectr_worker_res_t res;
	int ret = rp_spectr_get_params(&res);
	if (!ret)
	{
		*power = channel == 0 ? res.peak_pw_cha : res.peak_pw_chb;
	}

	return ret;
}

int spec_getPeakFreq(int channel, float* freq)
{
	rp_spectr_worker_res_t res;
	int ret = rp_spectr_get_params(&res);
	if (!ret)
	{
		*freq = channel == 0 ? res.peak_pw_freq_cha : res.peak_pw_freq_chb;
	}

	return ret;
}

int spec_setFreqRange(float freq)
{
	const float ranges[] = { 62500000, 7800000, 976000, 61000, 7600, 953 };
	int range = 0;
	for (; range < 5; ++range)
		if (freq > ranges[range])
			break;

	rp_spectr_worker_update_params_by_idx(range, FREQ_RANGE_PARAM, 1);

	fprintf(stderr, "max freq = %.2f range = %d\n", freq, range);

	return 0;
}

int spec_setUnit(int unit)
{
	rp_spectr_worker_update_params_by_idx(unit, FREQ_UNIT_PARAM, 1);
	rp_spectr_worker_update_params_by_idx(unit, PEAK_UNIT_CHA_PARAM, 1);
	rp_spectr_worker_update_params_by_idx(unit, PEAK_UNIT_CHB_PARAM, 1);

	return 0;
}

