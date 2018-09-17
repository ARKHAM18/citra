// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <thread>
#include <utility>
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/lock.h"

namespace Kernel {

HandleTable g_handle_table;

HandleTable::HandleTable() {
    Clear();
}

Handle HandleTable::Create(SharedPtr<Object> obj) {
    DEBUG_ASSERT(obj != nullptr);

    return objects.emplace(++handle_counter, std::move(obj)).first->first;
}

ResultVal<Handle> HandleTable::Duplicate(Handle handle) {
    SharedPtr<Object> object{GetGeneric(handle)};
    if (object == nullptr) {
        LOG_ERROR(Kernel, "Tried to duplicate invalid handle: {:08X}", handle);
        return ERR_INVALID_HANDLE;
    }
    return MakeResult<Handle>(Create(std::move(object)));
}

ResultCode HandleTable::Close(Handle handle) {
    if (!IsValid(handle))
        return ERR_INVALID_HANDLE;

    objects.erase(handle);

    return RESULT_SUCCESS;
}

bool HandleTable::IsValid(Handle handle) const {
    return objects.find(handle) != objects.end();
}

SharedPtr<Object> HandleTable::GetGeneric(Handle handle) const {
    if (handle == CurrentThread) {
        return GetCurrentThread();
    } else if (handle == CurrentProcess) {
        return g_current_process;
    }

    if (!IsValid(handle)) {
        return nullptr;
    }
    return objects.find(handle)->second;
}

void HandleTable::Clear() {
    objects.clear();
}

} // namespace Kernel
