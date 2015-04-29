/*
 * Copyright (c) 2014, AirBitz, Inc.
 * All rights reserved.
 *
 * See the LICENSE file for more information.
 */
/**
 * @file
 * AirBitz general, non-account-specific server-supplied data.
 *
 * The data handled in this file is basically just a local cache of various
 * settings that AirBitz would like to adjust from time-to-time without
 * upgrading the entire app.
 */

#include "General.hpp"
#include "http/AirbitzRequest.hpp"
#include "login/LoginServer.hpp"
#include "json/JsonObject.hpp"
#include "util/Debug.hpp"
#include "util/FileIO.hpp"
#include "util/Json.hpp"
#include "util/Util.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <jansson.h>

namespace abcd {

#define GENERAL_INFO_FILENAME                   "Servers.json"
#define GENERAL_QUESTIONS_FILENAME              "Questions.json"
#define GENERAL_ACCEPTABLE_INFO_FILE_AGE_SECS   (24 * 60 * 60) // how many seconds old can the info file before it should be updated

#define JSON_INFO_MINERS_FEES_FIELD             "minersFees"
#define JSON_INFO_MINERS_FEE_SATOSHI_FIELD      "feeSatoshi"
#define JSON_INFO_MINERS_FEE_TX_SIZE_FIELD      "txSizeBytes"
#define JSON_INFO_AIRBITZ_FEES_FIELD            "feesAirBitz"
#define JSON_INFO_AIRBITZ_FEE_PERCENTAGE_FIELD  "percentage"
#define JSON_INFO_AIRBITZ_FEE_MAX_SATOSHI_FIELD "maxSatoshi"
#define JSON_INFO_AIRBITZ_FEE_MIN_SATOSHI_FIELD "minSatoshi"
#define JSON_INFO_AIRBITZ_FEE_ADDRESS_FIELD     "address"
#define JSON_INFO_OBELISK_SERVERS_FIELD         "obeliskServers"
#define JSON_INFO_SYNC_SERVERS_FIELD            "syncServers"

#define ABC_SERVER_JSON_CATEGORY_FIELD      "category"
#define ABC_SERVER_JSON_MIN_LENGTH_FIELD    "min_length"
#define ABC_SERVER_JSON_QUESTION_FIELD      "question"

struct QuestionsFile:
    public JsonObject
{
    ABC_JSON_VALUE(questions, "questions", JsonPtr);
};

static tABC_CC ABC_GeneralGetInfoFilename(char **pszFilename, tABC_Error *pError);

/**
 * Frees the general info struct.
 */
void ABC_GeneralFreeInfo(tABC_GeneralInfo *pInfo)
{
    if (pInfo)
    {
        if ((pInfo->aMinersFees != NULL) && (pInfo->countMinersFees > 0))
        {
            for (unsigned i = 0; i < pInfo->countMinersFees; i++)
            {
                ABC_CLEAR_FREE(pInfo->aMinersFees[i], sizeof(tABC_GeneralMinerFee));
            }
            ABC_CLEAR_FREE(pInfo->aMinersFees, sizeof(tABC_GeneralMinerFee *) * pInfo->countMinersFees);
        }

        if (pInfo->pAirBitzFee)
        {
            ABC_FREE_STR(pInfo->pAirBitzFee->szAddresss);
            ABC_CLEAR_FREE(pInfo->pAirBitzFee, sizeof(tABC_GeneralMinerFee));
        }

        if ((pInfo->aszObeliskServers != NULL) && (pInfo->countObeliskServers > 0))
        {
            for (unsigned i = 0; i < pInfo->countObeliskServers; i++)
            {
                ABC_FREE_STR(pInfo->aszObeliskServers[i]);
            }
            ABC_CLEAR_FREE(pInfo->aszObeliskServers, sizeof(char *) * pInfo->countObeliskServers);
        }

        if ((pInfo->aszSyncServers != NULL) && (pInfo->countSyncServers > 0))
        {
            for (unsigned i = 0; i < pInfo->countSyncServers; i++)
            {
                ABC_FREE_STR(pInfo->aszSyncServers[i]);
            }
            ABC_CLEAR_FREE(pInfo->aszSyncServers, sizeof(char *) * pInfo->countSyncServers);
        }

        ABC_CLEAR_FREE(pInfo, sizeof(tABC_GeneralInfo));
    }
}

/**
 * Load the general info.
 *
 * This function will load the general info which includes information on
 * Obelisk Servers, AirBitz fees and miners fees.
 */
tABC_CC ABC_GeneralGetInfo(tABC_GeneralInfo **ppInfo,
                                   tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    JsonPtr file;
    json_t  *pJSON_Root             = NULL;
    json_t  *pJSON_Value            = NULL;
    char    *szInfoFilename         = NULL;
    tABC_GeneralInfo *pInfo         = NULL;
    json_t  *pJSON_MinersFeesArray  = NULL;
    json_t  *pJSON_AirBitzFees      = NULL;
    json_t  *pJSON_ObeliskArray     = NULL;
    json_t  *pJSON_SyncArray        = NULL;
    bool bExists = false;

    ABC_CHECK_NULL(ppInfo);

    // get the info filename
    ABC_CHECK_RET(ABC_GeneralGetInfoFilename(&szInfoFilename, pError));

    // check to see if we have the file
    ABC_CHECK_RET(ABC_FileIOFileExists(szInfoFilename, &bExists, pError));
    if (false == bExists)
    {
        // pull it down from the server
        ABC_CHECK_RET(ABC_GeneralUpdateInfo(pError));
    }

    // load the json
    ABC_CHECK_NEW(file.load(szInfoFilename), pError);
    pJSON_Root = file.get();

    // allocate the struct
    ABC_NEW(pInfo, tABC_GeneralInfo);

    // get the miners fees array
    pJSON_MinersFeesArray = json_object_get(pJSON_Root, JSON_INFO_MINERS_FEES_FIELD);
    ABC_CHECK_ASSERT((pJSON_MinersFeesArray && json_is_array(pJSON_MinersFeesArray)), ABC_CC_JSONError, "Error parsing JSON array value");

    // get the number of elements in the array
    pInfo->countMinersFees = (unsigned int) json_array_size(pJSON_MinersFeesArray);
    if (pInfo->countMinersFees > 0)
    {
        ABC_ARRAY_NEW(pInfo->aMinersFees, pInfo->countMinersFees, tABC_GeneralMinerFee*);
    }

    // run through all the miners fees
    for (unsigned i = 0; i < pInfo->countMinersFees; i++)
    {
        tABC_GeneralMinerFee *pFee = NULL;
        ABC_NEW(pFee, tABC_GeneralMinerFee);

        // get the source object
        json_t *pJSON_Fee = json_array_get(pJSON_MinersFeesArray, i);
        ABC_CHECK_ASSERT((pJSON_Fee && json_is_object(pJSON_Fee)), ABC_CC_JSONError, "Error parsing JSON array element object");

        // get the satoshi amount
        pJSON_Value = json_object_get(pJSON_Fee, JSON_INFO_MINERS_FEE_SATOSHI_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
        pFee->amountSatoshi = (int) json_integer_value(pJSON_Value);

        // get the tranaction size
        pJSON_Value = json_object_get(pJSON_Fee, JSON_INFO_MINERS_FEE_TX_SIZE_FIELD);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
        pFee->sizeTransaction = (int) json_integer_value(pJSON_Value);

        // assign this fee to the array
        pInfo->aMinersFees[i] = pFee;
    }

    // allocate the air bitz fees
    ABC_NEW(pInfo->pAirBitzFee, tABC_GeneralAirBitzFee);

    // get the air bitz fees object
    pJSON_AirBitzFees = json_object_get(pJSON_Root, JSON_INFO_AIRBITZ_FEES_FIELD);
    ABC_CHECK_ASSERT((pJSON_AirBitzFees && json_is_object(pJSON_AirBitzFees)), ABC_CC_JSONError, "Error parsing JSON object value");

    // get the air bitz fees percentage
    pJSON_Value = json_object_get(pJSON_AirBitzFees, JSON_INFO_AIRBITZ_FEE_PERCENTAGE_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_number(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON number value");
    pInfo->pAirBitzFee->percentage = json_number_value(pJSON_Value);

    // get the air bitz fees min satoshi
    pJSON_Value = json_object_get(pJSON_AirBitzFees, JSON_INFO_AIRBITZ_FEE_MIN_SATOSHI_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
    pInfo->pAirBitzFee->minSatoshi = json_integer_value(pJSON_Value);

    // get the air bitz fees max satoshi
    pJSON_Value = json_object_get(pJSON_AirBitzFees, JSON_INFO_AIRBITZ_FEE_MAX_SATOSHI_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_integer(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON integer value");
    pInfo->pAirBitzFee->maxSatoshi = json_integer_value(pJSON_Value);

    // get the air bitz fees address
    pJSON_Value = json_object_get(pJSON_AirBitzFees, JSON_INFO_AIRBITZ_FEE_ADDRESS_FIELD);
    ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
    ABC_STRDUP(pInfo->pAirBitzFee->szAddresss, json_string_value(pJSON_Value));


    // get the obelisk array
    pJSON_ObeliskArray = json_object_get(pJSON_Root, JSON_INFO_OBELISK_SERVERS_FIELD);
    ABC_CHECK_ASSERT((pJSON_ObeliskArray && json_is_array(pJSON_ObeliskArray)), ABC_CC_JSONError, "Error parsing JSON array value");

    // get the number of elements in the array
    pInfo->countObeliskServers = (unsigned int) json_array_size(pJSON_ObeliskArray);
    if (pInfo->countObeliskServers > 0)
    {
        ABC_ARRAY_NEW(pInfo->aszObeliskServers, pInfo->countObeliskServers, char*);
    }

    // run through all the obelisk servers
    for (unsigned i = 0; i < pInfo->countObeliskServers; i++)
    {
        // get the obelisk server
        pJSON_Value = json_array_get(pJSON_ObeliskArray, i);
        ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
        ABC_STRDUP(pInfo->aszObeliskServers[i], json_string_value(pJSON_Value));
    }

    // get the sync array
    pJSON_SyncArray = json_object_get(pJSON_Root, JSON_INFO_SYNC_SERVERS_FIELD);
    if (pJSON_SyncArray)
    {
        ABC_CHECK_ASSERT((pJSON_SyncArray && json_is_array(pJSON_SyncArray)), ABC_CC_JSONError, "Error parsing JSON array value");

        // get the number of elements in the array
        pInfo->countSyncServers = (unsigned int) json_array_size(pJSON_SyncArray);
        if (pInfo->countSyncServers > 0)
        {
            ABC_ARRAY_NEW(pInfo->aszSyncServers, pInfo->countSyncServers, char*);
        }

        // run through all the sync servers
        for (unsigned i = 0; i < pInfo->countSyncServers; i++)
        {
            // get the sync server
            pJSON_Value = json_array_get(pJSON_SyncArray, i);
            ABC_CHECK_ASSERT((pJSON_Value && json_is_string(pJSON_Value)), ABC_CC_JSONError, "Error parsing JSON string value");
            ABC_STRDUP(pInfo->aszSyncServers[i], json_string_value(pJSON_Value));
        }
    }
    else
    {
        pInfo->countSyncServers = 0;
        pInfo->aszSyncServers = NULL;
    }


    // assign the final result
    *ppInfo = pInfo;
    pInfo = NULL;

exit:
    ABC_FREE_STR(szInfoFilename);
    ABC_GeneralFreeInfo(pInfo);

    return cc;
}

/**
 * Update the general info from the server if needed and store it in the local file.
 *
 * This function will pull down info from the server including information on
 * Obelisk Servers, AirBitz fees and miners fees if the local file doesn't exist
 * or is out of date.
 */
tABC_CC ABC_GeneralUpdateInfo(tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    json_t  *pJSON_Root     = NULL;
    char    *szInfoFilename = NULL;
    char    *szJSON         = NULL;
    bool    bUpdateRequired = true;
    bool bExists = false;

    // get the info filename
    ABC_CHECK_RET(ABC_GeneralGetInfoFilename(&szInfoFilename, pError));

    // check to see if we have the file
    ABC_CHECK_RET(ABC_FileIOFileExists(szInfoFilename, &bExists, pError));
    if (true == bExists)
    {
        // check to see if the file is too old

        // get the current time
        time_t timeNow = time(NULL);

        // get the time the file was last changed
        time_t timeFileMod;
        ABC_CHECK_RET(ABC_FileIOFileModTime(szInfoFilename, &timeFileMod, pError));

        // if it isn't too old then don't update
        if ((timeNow - timeFileMod) < GENERAL_ACCEPTABLE_INFO_FILE_AGE_SECS)
        {
            bUpdateRequired = false;
        }
    }

    // if we need to update
    if (bUpdateRequired)
    {
        JsonPtr infoJson;
        ABC_CHECK_NEW(loginServerGetGeneral(infoJson), pError);
        ABC_CHECK_NEW(infoJson.save(szInfoFilename), pError);
    }

exit:
    if (pJSON_Root)     json_decref(pJSON_Root);
    ABC_FREE_STR(szInfoFilename);
    ABC_FREE_STR(szJSON);

    return cc;
}

/*
 * Gets the general info filename
 *
 * @param pszFilename Location to store allocated filename string (caller must free)
 */
static
tABC_CC ABC_GeneralGetInfoFilename(char **pszFilename,
                                   tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    std::string filename = getRootDir() + GENERAL_INFO_FILENAME;

    ABC_CHECK_NULL(pszFilename);
    ABC_STRDUP(*pszFilename, filename.c_str());

exit:
    return cc;
}

/**
 * Free question choices.
 *
 * This function frees the question choices given
 *
 * @param pQuestionChoices  Pointer to question choices to free.
 */
void ABC_GeneralFreeQuestionChoices(tABC_QuestionChoices *pQuestionChoices)
{
    if (pQuestionChoices != NULL)
    {
        if ((pQuestionChoices->aChoices != NULL) && (pQuestionChoices->numChoices > 0))
        {
            for (unsigned i = 0; i < pQuestionChoices->numChoices; i++)
            {
                tABC_QuestionChoice *pChoice = pQuestionChoices->aChoices[i];

                if (pChoice)
                {
                    ABC_FREE_STR(pChoice->szQuestion);
                    ABC_FREE_STR(pChoice->szCategory);
                }
            }

            ABC_CLEAR_FREE(pQuestionChoices->aChoices, sizeof(tABC_QuestionChoice *) * pQuestionChoices->numChoices);
        }

        ABC_CLEAR_FREE(pQuestionChoices, sizeof(tABC_QuestionChoices));
    }
}

/**
 * Gets the recovery question chioces with the given info.
 *
 * @param ppQuestionChoices Pointer to hold allocated pointer to recovery question chioces
 */
tABC_CC ABC_GeneralGetQuestionChoices(tABC_QuestionChoices    **ppQuestionChoices,
                                      tABC_Error              *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    std::string filename = getRootDir() + GENERAL_QUESTIONS_FILENAME;
    QuestionsFile file;
    json_t *pJSON_Value = NULL;
    tABC_QuestionChoices *pQuestionChoices = NULL;
    bool bExists = false;
    unsigned int count = 0;

    ABC_CHECK_NULL(ppQuestionChoices);

    // if the file doesn't exist
    ABC_CHECK_RET(ABC_FileIOFileExists(filename.c_str(), &bExists, pError));
    if (true != bExists)
    {
        // get an update from the server
        ABC_CHECK_RET(ABC_GeneralUpdateQuestionChoices(pError));
    }

    // Read in the recovery question choices json object
    ABC_CHECK_NEW(file.load(filename), pError);
    pJSON_Value = file.questions().get();
    if (!json_is_array(pJSON_Value))
        ABC_RET_ERROR(ABC_CC_JSONError, "No questions in the recovery question choices file")

    // get the number of elements in the array
    count = (unsigned int) json_array_size(pJSON_Value);
    if (count <= 0)
    {
        ABC_RET_ERROR(ABC_CC_JSONError, "No questions in the recovery question choices file")
    }

    // allocate the data
    ABC_NEW(pQuestionChoices, tABC_QuestionChoices);
    pQuestionChoices->numChoices = count;
    ABC_ARRAY_NEW(pQuestionChoices->aChoices, count, tABC_QuestionChoice*);

    for (unsigned i = 0; i < count; i++)
    {
        json_t *pJSON_Elem = json_array_get(pJSON_Value, i);
        ABC_CHECK_ASSERT((pJSON_Elem && json_is_object(pJSON_Elem)), ABC_CC_JSONError, "Error parsing JSON element value for recovery questions");

        // allocate this element
        ABC_NEW(pQuestionChoices->aChoices[i], tABC_QuestionChoice);

        // get the category
        json_t *pJSON_Obj = json_object_get(pJSON_Elem, ABC_SERVER_JSON_CATEGORY_FIELD);
        ABC_CHECK_ASSERT((pJSON_Obj && json_is_string(pJSON_Obj)), ABC_CC_JSONError, "Error parsing JSON category value for recovery questions");
        ABC_STRDUP(pQuestionChoices->aChoices[i]->szCategory, json_string_value(pJSON_Obj));

        // get the question
        pJSON_Obj = json_object_get(pJSON_Elem, ABC_SERVER_JSON_QUESTION_FIELD);
        ABC_CHECK_ASSERT((pJSON_Obj && json_is_string(pJSON_Obj)), ABC_CC_JSONError, "Error parsing JSON question value for recovery questions");
        ABC_STRDUP(pQuestionChoices->aChoices[i]->szQuestion, json_string_value(pJSON_Obj));

        // get the min length
        pJSON_Obj = json_object_get(pJSON_Elem, ABC_SERVER_JSON_MIN_LENGTH_FIELD);
        ABC_CHECK_ASSERT((pJSON_Obj && json_is_integer(pJSON_Obj)), ABC_CC_JSONError, "Error parsing JSON min length value for recovery questions");
        pQuestionChoices->aChoices[i]->minAnswerLength = (unsigned int) json_integer_value(pJSON_Obj);
    }

    // assign final data
    *ppQuestionChoices = pQuestionChoices;
    pQuestionChoices = NULL; // so we don't free it below

exit:
    if (pQuestionChoices) ABC_GeneralFreeQuestionChoices(pQuestionChoices);

    return cc;
}

/**
 * Gets the recovery question choices from the server and saves them
 * to local storage.
 *
 * @param szUserName UserName for a valid account to retrieve questions
 */
tABC_CC ABC_GeneralUpdateQuestionChoices(tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    JsonPtr resultsJson;
    QuestionsFile file;

    // get the questions from the server
    ABC_CHECK_NEW(loginServerGetQuestions(resultsJson), pError);
    ABC_CHECK_NEW(file.questionsSet(resultsJson), pError);
    ABC_CHECK_NEW(file.save(getRootDir() + GENERAL_QUESTIONS_FILENAME), pError);

exit:
    return cc;
}

} // namespace abcd
