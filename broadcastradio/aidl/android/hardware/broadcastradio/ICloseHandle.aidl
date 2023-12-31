/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.hardware.broadcastradio;

/**
 * Represents a generic close handle to remove a callback that doesn't need
 * active interface.
 */
@VintfStability
interface ICloseHandle {
    /**
     * Closes the handle.
     *
     * The call must not fail and must only be issued once.
     *
     * After the close call is executed, no other calls to this interface
     * are allowed. If the call is issued second time, a service-specific
     * error {@link Result#INVALID_STATE} will be returned.
     */
    void close();
}
