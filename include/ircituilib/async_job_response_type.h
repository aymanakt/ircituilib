//
// Created by ayman on 15/09/2026.
//

/**
 * @file async_job_response_type.h
 * @brief async_job_response_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ircituilib source code.
 * Created by ayman on 15/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef IRCITUILIB_ASYNC_JOB_RESPONSE_TYPE_H
#define IRCITUILIB_ASYNC_JOB_RESPONSE_TYPE_H

#define MAX_ERROR_MESSAGE_LEN 128

#include "ircit_ui_node_type.h"

/**
 * @brief Thread-safe pipeline response capsule.
 *        Carries completed out-of-band execution data back to Thread 0.
 */
typedef struct {
  /**
   * @brief Direct pointer to the live target node that requested this operation.
   *        Ensures an O(1) constant-time reference match during the dispatch phase.
   */
  IrcitUiNode *target;

  /**
   * @brief Operational execution return code flag.
   *        0 = Success; Negative integers indicate explicit infrastructure or network errors.
   */
  int status_code;

  /**
   * @brief Safe character buffer space to hold failure diagnostic strings.
   *        Populated by background worker coroutines and read natively by the UI thread.
   */
  char error_message[MAX_ERROR_MESSAGE_LEN];
} AsyncJobResponse;

#endif //IRCITUILIB_ASYNC_JOB_RESPONSE_TYPE_H
