/*-
 * Copyright (c) 2012-2013 Jan Breuer,
 *
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file   scpi_parser.c
 * @date   Thu Nov 15 10:58:45 UTC 2012
 *
 * @brief  SCPI parser implementation
 *
 *
 */

#include <string.h>
#include <sys/socket.h>
#include <elf.h>
#include <stdio.h>

#include "scpi/config.h"
#include "scpi/parser.h"
#include "scpi/utils_private.h"
#include "scpi/error.h"
#include "scpi/constants.h"
#include "../inc/scpi/types.h"
#include "../inc/scpi/utils_private.h"
#include "../inc/scpi/error.h"


static size_t cmdTerminatorPos(const char * cmd, size_t len);
static size_t cmdlineSeparatorPos(const char * cmd, size_t len);
static const char * cmdlineSeparator(const char * cmd, size_t len);
static const char * cmdlineTerminator(const char * cmd, size_t len);
static size_t skipCmdLine(const char * cmd, size_t len);

static void paramSkipBytes(scpi_t * context, size_t num);
static void paramSkipWhitespace(scpi_t * context);
static scpi_bool_t paramNext(scpi_t * context, scpi_bool_t mandatory);

/*
int _strnicmp(const char* s1, const char* s2, size_t len) {
    int result = 0;
    int i;

    for (i = 0; i < len && s1[i] && s2[i]; i++) {
        char c1 = tolower(s1[i]);
        char c2 = tolower(s2[i]);
        if (c1 != c2) {
            result = (int) c1 - (int) c2;
            break;
        }
    }

    return result;
}
 */

/**
 * Find command termination character
 * @param cmd - input command
 * @param len - max search length
 * @return position of terminator or len
 */
size_t cmdTerminatorPos(const char * cmd, size_t len) {
    const char * terminator = strnpbrk(cmd, len, "; \r\n\t");
    if (terminator == NULL) {
        return len;
    } else {
        return terminator - cmd;
    }
}

/**
 * Find command line separator
 * @param cmd - input command
 * @param len - max search length
 * @return pointer to line separator or NULL
 */
const char * cmdlineSeparator(const char * cmd, size_t len) {
    return strnpbrk(cmd, len, ";\r\n");
}

/**
 * Find command line terminator
 * @param cmd - input command
 * @param len - max search length
 * @return pointer to command line terminator or NULL
 */
const char * cmdlineTerminator(const char * cmd, size_t len) {
    return strnpbrk(cmd, len, "\r\n");
}

/**
 * Find command line separator position
 * @param cmd - input command
 * @param len - max search length
 * @return position of line separator or len
 */
size_t cmdlineSeparatorPos(const char * cmd, size_t len) {
    const char * separator = cmdlineSeparator(cmd, len);
    if (separator == NULL) {
        return len;
    } else {
        return separator - cmd;
    }
}

/**
 * Find next part of command
 * @param cmd - input command
 * @param len - max search length
 * @return number of characters to be skipped
 */
size_t skipCmdLine(const char * cmd, size_t len) {
    const char * separator = cmdlineSeparator(cmd, len);
    if (separator == NULL) {
        return len;
    } else {
        return separator + 1 - cmd;
    }
}

/**
 * Write data to SCPI output
 * @param context
 * @param data
 * @param len - lenght of data to be written
 * @return number of bytes written
 */
static size_t writeData(scpi_t * context, const char * data, size_t len) {
    return context->interface->write(context, data, len);
}

/**
 * Flush data to SCPI output
 * @param context
 * @return
 */
static int flushData(scpi_t * context) {
    if (context && context->interface && context->interface->flush) {
        return context->interface->flush(context);
    } else {
        return SCPI_RES_OK;
    }
}

/**
 * Write result delimiter to output
 * @param context
 * @return number of bytes written
 */
static size_t writeDelimiter(scpi_t * context) {
    if (context->output_count > 0) {
        return writeData(context, ", ", 2);
    } else {
        return 0;
    }
}

/**
 * Conditionaly write "New Line"
 * @param context
 * @return number of characters written
 */
static size_t writeNewLine(scpi_t * context) {
    if (context->output_count > 0) {
        size_t len;
        len = writeData(context, "\r\n", 2);
        flushData(context);
        return len;
    } else if (context->output_binary_count > 0) {
        flushData(context);
    }
    return 0;
}

/**
 * Writes header for binary data
 * @param context
 * @param numElems - number of items in the array
 * @param sizeOfElem - size of each item [sizeof(float), sizeof(int), ...]
 * @return number of characters written
 */
size_t writeBinHeader(scpi_t * context, uint32_t numElems, size_t sizeOfElem) {

    size_t result = 0;
    char numBytes[10];
    char numOfNumBytes[2];

    // Calculate number of bytes needed for all elements
    size_t numDataBytes = numElems * sizeOfElem;

    // Do not allow more than 9 character long size
    if (numDataBytes > 999999999){
        return result;
    }

    // Convert to string and calculate string length
    size_t len = longToStr(numDataBytes, numBytes, sizeof(numBytes));

    // Convert len to sting
    longToStr(len, numOfNumBytes, sizeof(numOfNumBytes));

    result += writeData(context, "#", 1);
    result += writeData(context, numOfNumBytes, 1);
    result += writeData(context, numBytes, len);

    return result;
}


/**
 * Process command
 * @param context
 */
static void processCommand(scpi_t * context) {
    const scpi_command_t * cmd = context->paramlist.cmd;

    context->cmd_error = FALSE;
    context->output_count = 0;
    context->output_binary_count = 0;
    context->input_count = 0;

    SCPI_DEBUG_COMMAND(context);
    /* if callback exists - call command callback */
    if (cmd->callback != NULL) {
        if ((cmd->callback(context) != SCPI_RES_OK) && !context->cmd_error) {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        }
    }

    /* conditionaly write new line */
    writeNewLine(context);

    /* skip all whitespaces */
    paramSkipWhitespace(context);

    /* set error if command callback did not read all parameters */
    if (context->paramlist.length != 0 && !context->cmd_error) {
        SCPI_ErrorPush(context, SCPI_ERROR_PARAMETER_NOT_ALLOWED);
    }
}

/**
 * Cycle all patterns and search matching pattern. Execute command callback.
 * @param context
 * @result TRUE if context->paramlist is filled with correct values
 */
static scpi_bool_t findCommand(scpi_t * context, const char * cmdline_ptr, size_t cmdline_len, size_t cmd_len) {
    int32_t i;
    const scpi_command_t * cmd;

    for (i = 0; context->cmdlist[i].pattern != NULL; i++) {
        cmd = &context->cmdlist[i];
        if (matchCommand(cmd->pattern, cmdline_ptr, cmd_len)) {
            context->paramlist.cmd = cmd;
            context->paramlist.parameters = cmdline_ptr + cmd_len;
            context->paramlist.length = cmdline_len - cmd_len;
            context->paramlist.cmd_raw.data = cmdline_ptr;
            context->paramlist.cmd_raw.length = cmd_len;
            context->paramlist.cmd_raw.position = 0;
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Parse one command line
 * @param context
 * @param data - complete command line
 * @param len - command line length
 * @return 1 if the last evaluated command was found
 */
int SCPI_Parse(scpi_t * context, char * data, size_t len) {
    int result = 0;
    const char * cmdline_end = data + len;
    char * cmdline_ptr = data;
    size_t cmd_len;
    size_t cmdline_len;
    char * cmdline_ptr_prev = NULL;
    size_t cmd_len_prev = 0;

    if (context == NULL) {
        return -1;
    }

    while (cmdline_ptr < cmdline_end) {
        result = 0;
        cmd_len = cmdTerminatorPos(cmdline_ptr, cmdline_end - cmdline_ptr);
        if (cmd_len > 0) {
            composeCompoundCommand(cmdline_ptr_prev, cmd_len_prev,
                                    &cmdline_ptr, &cmd_len);
            cmdline_len = cmdlineSeparatorPos(cmdline_ptr, cmdline_end - cmdline_ptr);
            if(findCommand(context, cmdline_ptr, cmdline_len, cmd_len)) {
                processCommand(context);
                result = 1;
                cmdline_ptr_prev = cmdline_ptr;
                cmd_len_prev = cmd_len;
            } else {
                SCPI_ErrorPush(context, SCPI_ERROR_UNDEFINED_HEADER);
            }
        }
        cmdline_ptr += skipCmdLine(cmdline_ptr, cmdline_end - cmdline_ptr);
        cmdline_ptr += skipWhitespace(cmdline_ptr, cmdline_end - cmdline_ptr);
    }
    return result;
}

/**
 * Initialize SCPI context structure
 * @param context
 * @param command_list
 * @param buffer
 * @param interface
 */
void SCPI_Init(scpi_t * context) {
    if (context->idn[0] == NULL) {
        context->idn[0] = SCPI_DEFAULT_1_MANUFACTURE;
    }
    if (context->idn[1] == NULL) {
        context->idn[1] = SCPI_DEFAULT_2_MODEL;
    }
    if (context->idn[2] == NULL) {
        context->idn[2] = SCPI_DEFAULT_3;
    }
    if (context->idn[3] == NULL) {
        context->idn[3] = SCPI_DEFAULT_4_REVISION;
    }

    context->buffer.position = 0;
    SCPI_ErrorInit(context);
}

/**
 * Interface to the application. Adds data to system buffer and try to search
 * command line termination. If the termination is found or if len=0, command
 * parser is called.
 *
 * @param context
 * @param data - data to process
 * @param len - length of data
 * @return
 */
int SCPI_Input(scpi_t * context, const char * data, size_t len) {
    int result = 0;
    const char * cmd_term;
    if (len == 0) {
        context->buffer.data[context->buffer.position] = 0;
        result = SCPI_Parse(context, context->buffer.data, context->buffer.position);
        context->buffer.position = 0;
    } else {
        size_t buffer_free;
        int ws;
        buffer_free = context->buffer.length - context->buffer.position;
        if (len > (buffer_free - 1)) {
            return -1;
        }
        memcpy(&context->buffer.data[context->buffer.position], data, len);
        context->buffer.position += len;
        context->buffer.data[context->buffer.position] = 0;

        ws = skipWhitespace(context->buffer.data, context->buffer.position);
        cmd_term = cmdlineTerminator(context->buffer.data + ws, context->buffer.position - ws);
        while (cmd_term != NULL) {
            int curr_len = cmd_term - context->buffer.data;
            result = SCPI_Parse(context, context->buffer.data + ws, curr_len - ws);
            memmove(context->buffer.data, cmd_term, context->buffer.position - curr_len);
            context->buffer.position -= curr_len;

            ws = skipWhitespace(context->buffer.data, context->buffer.position);
            cmd_term = cmdlineTerminator(context->buffer.data + ws, context->buffer.position - ws);
        }
    }

    return result;
}

/* writing results */

/**
 * Write raw string result to the output
 * @param context
 * @param data
 * @return
 */
size_t SCPI_ResultString(scpi_t * context, const char * data) {
    size_t len = strlen(data);
    size_t result = 0;
    result += writeDelimiter(context);
    result += writeData(context, data, len);
    context->output_count++;
    return result;
}

/**
 * Write integer value to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultInt(scpi_t * context, int32_t val) {
    char buffer[15];
    size_t result = 0;
    size_t len = longToStr(val, buffer, sizeof (buffer));
    result += writeDelimiter(context);
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;
}

size_t SCPI_ResultUInt(scpi_t *context, uint32_t val) {
    char buffer[15];
    size_t result = 0;
    size_t len = longToStr(val, buffer, sizeof (buffer));
    result += writeDelimiter(context);
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;
}

/**
* Write long value to the result
* @param context
* @param val
* @return
*/
size_t SCPI_ResultLong(scpi_t * context, int64_t val) {
    char buffer[25];
    size_t result = 0;
    size_t len = longToStr(val, buffer, sizeof (buffer));
    result += writeDelimiter(context);
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;
}

size_t SCPI_ResultULong(scpi_t *context, uint64_t val) {
    char buffer[25];
    size_t result = 0;
    size_t len = longToStr(val, buffer, sizeof (buffer));
    result += writeDelimiter(context);
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;
}

/**
 * Write boolean value to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultBool(scpi_t * context, scpi_bool_t val) {
	return SCPI_ResultInt(context, val ? 1 : 0);
}

/**
 * Write double walue to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultDouble(scpi_t * context, double val) {
    char buffer[32];
    size_t result = 0;
    size_t len = doubleToStr(val, buffer, sizeof (buffer));
    result += writeDelimiter(context);
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;

}

/**
 * Write string withn " to the result
 * @param context
 * @param data
 * @return
 */
size_t SCPI_ResultText(scpi_t * context, const char * data) {
    size_t result = 0;
    result += writeDelimiter(context);
    result += writeData(context, "\"", 1);
    result += writeData(context, data, strlen(data));
    result += writeData(context, "\"", 1);
    context->output_count++;
    return result;
}

size_t resultBufferInt16Bin(scpi_t * context, const int16_t *data, uint32_t size) {
    size_t result = 0;

    result += writeBinHeader(context, size, sizeof(float));

    if (result == 0) {
        return result;
    }

    uint32_t i;
    for (i = 0; i < size; i++) {
        int16_t value = htons(data[i]);
        result += writeData(context, (char*)(&value), sizeof(int16_t));
    }
    context->output_binary_count++;
    return result;
}

size_t resultBufferInt16Ascii(scpi_t * context, const int16_t *data, uint32_t size) {
    size_t result = 0;
    result += writeDelimiter(context);
    result += writeData(context, "{", 1);

    uint32_t i;
    size_t len;
    char buffer[12];
    for (i = 0; i < size-1; i++) {
        len = longToStr(data[i], buffer, sizeof (buffer));
        result += writeData(context, buffer, len);
        result += writeData(context, ",", 1);
    }
    len = longToStr(data[i], buffer, sizeof (buffer));
    result += writeData(context, buffer, len);
    result += writeData(context, "}", 1);
    context->output_count++;
    return result;
}


size_t SCPI_ResultBufferInt16(scpi_t * context, const int16_t *data, uint32_t size) {

    if (context->binary_output == true) {
        return resultBufferInt16Bin(context, data, size);
    }
    else {
        return resultBufferInt16Ascii(context, data, size);
    }
}

size_t resultBufferFloatBin(scpi_t * context, const float *data, uint32_t size) {
    size_t result = 0;

    result += writeBinHeader(context, size, sizeof(float));

    if (result == 0) {
        return result;
    }

    uint32_t i;
    for (i = 0; i < size; i++) {
        float value = hton_f(data[i]);
        result += writeData(context, (char*)(&value), sizeof(float));
    }
    context->output_binary_count++;
    return result;
}


size_t resultBufferFloatAscii(scpi_t * context, const float *data, uint32_t size) {
    size_t result = 0;
    result += writeDelimiter(context);
    result += writeData(context, "{", 1);

    uint32_t i;
    size_t len;
    char buffer[50];
    for (i = 0; i < size-1; i++) {
        len = doubleToStr(data[i], buffer, sizeof (buffer));
        result += writeData(context, buffer, len);
        result += writeData(context, ",", 1);
    }
    len = doubleToStr(data[i], buffer, sizeof (buffer));
    result += writeData(context, buffer, len);
    result += writeData(context, "}", 1);
    context->output_count++;
    return result;
}

size_t SCPI_ResultBufferFloat(scpi_t * context, const float *data, uint32_t size) {

    if (context->binary_output == true) {
        return resultBufferFloatBin(context, data, size);
    }
    else {
        return resultBufferFloatAscii(context, data, size);
    }
}


/* parsing parameters */

/**
 * Skip num bytes from the begginig of parameters
 * @param context
 * @param num
 */
void paramSkipBytes(scpi_t * context, size_t num) {
    if (context->paramlist.length < num) {
        num = context->paramlist.length;
    }
    context->paramlist.parameters += num;
    context->paramlist.length -= num;
}

/**
 * Skip white spaces from the beggining of parameters
 * @param context
 */
void paramSkipWhitespace(scpi_t * context) {
    size_t ws = skipWhitespace(context->paramlist.parameters, context->paramlist.length);
    paramSkipBytes(context, ws);
}

/**
 * Find next parameter
 * @param context
 * @param mandatory
 * @return
 */
scpi_bool_t paramNext(scpi_t * context, scpi_bool_t mandatory) {
    paramSkipWhitespace(context);
    if (context->paramlist.length == 0) {
        if (mandatory) {
            SCPI_ErrorPush(context, SCPI_ERROR_MISSING_PARAMETER);
        }
        return FALSE;
    }
    if (context->input_count != 0) {
        if (context->paramlist.parameters[0] == ',') {
            paramSkipBytes(context, 1);
            paramSkipWhitespace(context);
        } else {
            SCPI_ErrorPush(context, SCPI_ERROR_INVALID_SEPARATOR);
            return FALSE;
        }
    }
    context->input_count++;
    return TRUE;
}

/**
 * Parse integer parameter
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamInt(scpi_t * context, int32_t * value, scpi_bool_t mandatory) {
    const char * param;
    size_t param_len;
    size_t num_len;

    if (!value) {
        return FALSE;
    }

    if (!SCPI_ParamString(context, &param, &param_len, mandatory)) {
        return FALSE;
    }

    num_len = strToLong(param, value);

    if (num_len != param_len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
        return FALSE;
    }

    return TRUE;
}


scpi_bool_t SCPI_ParamUInt(scpi_t *context, uint32_t *value, scpi_bool_t mandatory) {
    const char * param;
    size_t param_len;
    size_t num_len;

    if (!value) {
        return FALSE;
    }

    if (!SCPI_ParamString(context, &param, &param_len, mandatory)) {
        return FALSE;
    }

    num_len = strToLong(param, value);

    if (num_len != param_len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
        return FALSE;
    }

    return TRUE;
}

scpi_bool_t SCPI_ParamLong(scpi_t *context, int64_t *value, scpi_bool_t mandatory) {
    const char * param;
    size_t param_len;
    size_t num_len;

    if (!value) {
        return FALSE;
    }

    if (!SCPI_ParamString(context, &param, &param_len, mandatory)) {
        return FALSE;
    }

    num_len = strToLongLong(param, value);

    if (num_len != param_len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
        return FALSE;
    }

    return TRUE;
}

scpi_bool_t SCPI_ParamULong(scpi_t *context, uint64_t *value, scpi_bool_t mandatory) {
    const char * param;
    size_t param_len;
    size_t num_len;

    if (!value) {
        return FALSE;
    }

    if (!SCPI_ParamString(context, &param, &param_len, mandatory)) {
        return FALSE;
    }

    num_len = strToLongLong(param, value);

    if (num_len != param_len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
        return FALSE;
    }

    return TRUE;
}


/**
 * Parse double parameter
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamDouble(scpi_t * context, double * value, scpi_bool_t mandatory) {
    const char * param;
    size_t param_len;
    size_t num_len;

    if (!value) {
        return FALSE;
    }

    if (!SCPI_ParamString(context, &param, &param_len, mandatory)) {
        return FALSE;
    }

    num_len = strToDouble(param, value);

    if (num_len != param_len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
        return FALSE;
    }

    return TRUE;
}

/**
 * Parse string parameter
 * @param context
 * @param value Pointer to string buffer where pointer to non-null terminated string will be returned
 * @param len Length of returned non-null terminated string
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamString(scpi_t * context, const char ** value, size_t * len, scpi_bool_t mandatory) {
    size_t length;

    if (!value || !len) {
        return FALSE;
    }

    if (!paramNext(context, mandatory)) {
        return FALSE;
    }

    if (locateStr(context->paramlist.parameters, context->paramlist.length, value, &length)) {
        paramSkipBytes(context, length);
        paramSkipWhitespace(context);
        if (len) {
            *len = length;
        }
        return TRUE;
    }

    return FALSE;
}

/**
 * Parse text parameter (can be inside "")
 * @param context
 * @param value Pointer to string buffer where pointer to non-null terminated string will be returned
 * @param len Length of returned non-null terminated string
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamText(scpi_t * context, const char ** value, size_t * len, scpi_bool_t mandatory) {
    size_t length;

    if (!value || !len) {
        return FALSE;
    }

    if (!paramNext(context, mandatory)) {
        return FALSE;
    }

    if (locateText(context->paramlist.parameters, context->paramlist.length, value, &length)) {
        paramSkipBytes(context, length);
        if (len) {
            *len = length;
        }
        return TRUE;
    }

    return FALSE;
}

/**
 * Parse boolean parameter as described in the spec SCPI-99 7.3 Boolean Program Data
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamBool(scpi_t * context, scpi_bool_t * value, scpi_bool_t mandatory) {
    const char * param;
    size_t param_len;
    size_t num_len;
    int32_t i;

    if (!value) {
        return FALSE;
    }

    if (!SCPI_ParamString(context, &param, &param_len, mandatory)) {
        return FALSE;
    }

    if (matchPattern("ON", 2, param, param_len)) {
        *value = TRUE;
    } else if (matchPattern("OFF", 3, param, param_len)) {
        *value = FALSE;
    } else {
        num_len = strToLong(param, &i);

        if (num_len != param_len) {
            SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
            return FALSE;
        }

        *value = i ? TRUE : FALSE;
    }

    return TRUE;
}

/**
 * Parse choice parameter
 * @param context
 * @param options
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamChoice(scpi_t * context, const char * options[], int32_t * value, scpi_bool_t mandatory) {
    const char * param;
    size_t param_len;
    size_t res;

    if (!options || !value) {
        return FALSE;
    }

    if (!SCPI_ParamString(context, &param, &param_len, mandatory)) {
        return FALSE;
    }

    for (res = 0; options[res]; ++res) {
        if (matchPattern(options[res], strlen(options[res]), param, param_len)) {
            *value = res;
            return TRUE;
        }
    }

    SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
    return FALSE;
}


size_t SCPI_ParamBufferFloat(scpi_t * context, float *data, uint32_t *size, scpi_bool_t mandatory) {
    *size = 0;
    double value;
    while (true) {
        if (!SCPI_ParamDouble(context, &value, mandatory)) {
            break;
        }
        data[*size] = (float) value;
        *size = *size + 1;
        mandatory = false;          // only first is mandatory
    }
    return true;
}


scpi_bool_t SCPI_IsCmd(scpi_t * context, const char * cmd) {
    if (! context->paramlist.cmd) {
        return FALSE;
    }

    const char * pattern = context->paramlist.cmd->pattern;
    return matchCommand (pattern, cmd, strlen (cmd));
}
