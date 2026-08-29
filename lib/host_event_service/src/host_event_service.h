#pragma once

#include <stdint.h>

#include "host_operation_service.h"
#include "hub_event_ledger.h"

namespace HostEventService {

class Service {
public:
    explicit Service(HubEventLedger::Ledger& ledger,
                     RuntimeState::State* diagnostics = nullptr) :
        ledger_(ledger), diagnostics_(diagnostics) {}

    HostOperationService::Result handle(
        uint16_t requestId,
        const HostProtocol::OperationRequest& request,
        RuntimeState::DeviceRole role,
        uint8_t localDeviceId
    ) {
        if (request.category != HostProtocol::OperationCategory::EVENT) {
            HostOperationService::Result result =
                HostOperationService::makeBaseResult(requestId, request);
            result.disposition = HostOperationService::Disposition::NOT_HANDLED;
            return result;
        }
        if (role != RuntimeState::DeviceRole::HUB) {
            return HostOperationService::reject(requestId, request,
                HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION, true);
        }
        if (request.targetDeviceId != localDeviceId) {
            return HostOperationService::reject(requestId, request,
                HostProtocol::RequestRejectionCode::BAD_TARGET, true);
        }
        if (HostProtocol::validateOperationRequest(
                HostProtocol::VERSION_MINOR_0_2, request) !=
            HostProtocol::PayloadResult::OK) {
            return HostOperationService::reject(requestId, request,
                HostProtocol::RequestRejectionCode::MALFORMED_REQUEST, true);
        }

        HostOperationService::Result result =
            HostOperationService::makeBaseResult(requestId, request);
        result.disposition = HostOperationService::Disposition::HANDLED;
        if (!ledger_.healthy()) {
            setEventFailure(result, HostProtocol::EventResultCode::STORAGE_FAILURE);
            return result;
        }
        if (request.operation == HostProtocol::OperationCode::POLL_EVENTS) {
            return poll(result);
        }
        if (request.operation == HostProtocol::OperationCode::CONSUME_EVENT) {
            return consume(result, request);
        }
        return HostOperationService::reject(requestId, request,
            HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION, true);
    }

private:
    static void setSuccess(HostOperationService::Result& result) {
        result.response.resultClass = HostProtocol::ResultClass::SUCCESS;
        result.response.resultCode = static_cast<uint8_t>(
            HostProtocol::SuccessCode::OK);
    }

    static void setEventFailure(HostOperationService::Result& result,
        HostProtocol::EventResultCode code) {
        result.response.resultClass = HostProtocol::ResultClass::EVENT_RESULT;
        result.response.resultCode = static_cast<uint8_t>(code);
        HostProtocol::setNoneValue(result.response.value);
    }

    static bool convert(const EventRecords::HubRecord& source,
        HostProtocol::HostEventRecord& target) {
        if (source.state != EventRecords::HubState::ACTIVE) return false;
        HostProtocol::HostEventRecord candidate = {};
        candidate.available = 1;
        candidate.sourceDeviceId = source.sourceDeviceId;
        candidate.family = source.family;
        candidate.flags = source.flags;
        candidate.eventEpoch = source.eventEpoch;
        candidate.eventId = source.eventId;
        candidate.lifetimeBudgetSeconds = source.lifetimeBudgetSeconds;
        candidate.bodyLength = source.bodyLength;
        for (uint8_t i = 0; i < source.bodyLength; ++i)
            candidate.body[i] = source.body[i];
        if (!HostProtocol::isValidHostEventRecord(candidate)) return false;
        target = candidate;
        return true;
    }

    HostOperationService::Result poll(HostOperationService::Result result) {
        HostProtocol::HostEventRecord event = {};
        uint8_t slot = 0;
        EventRecords::HubRecord record = {};
        if (ledger_.oldestActive(slot, record) && !convert(record, event)) {
            setEventFailure(result, HostProtocol::EventResultCode::STORAGE_FAILURE);
            return result;
        }
        setSuccess(result);
        size_t length = 0;
        result.response.value.type = HostProtocol::STRUCTURE_VALUE_TYPE;
        if (HostProtocol::encodeHostEventRecord(event, result.response.value.bytes,
                sizeof(result.response.value.bytes), length) !=
            HostProtocol::PayloadResult::OK) {
            setEventFailure(result, HostProtocol::EventResultCode::STORAGE_FAILURE);
            return result;
        }
        result.response.value.length = static_cast<uint8_t>(length);
        if (event.available == 1 && diagnostics_ != nullptr) {
            diagnostics_->incrementEventDiagnostic(
                RuntimeState::EventDiagnostic::HOST_POLL_RETURNED_EVENT);
        }
        return result;
    }

    HostOperationService::Result consume(HostOperationService::Result result,
        const HostProtocol::OperationRequest& request) {
        HostProtocol::HostEventIdentity hostIdentity = {};
        if (HostProtocol::decodeHostEventIdentity(request.value.bytes,
                request.value.length,
                hostIdentity) != HostProtocol::PayloadResult::OK) {
            return HostOperationService::reject(result.requestId, request,
                HostProtocol::RequestRejectionCode::MALFORMED_REQUEST, true);
        }
        EventProtocol::Identity identity = {
            hostIdentity.sourceDeviceId, hostIdentity.eventEpoch, hostIdentity.eventId};
        const HubEventLedger::ConsumeResult consumed = ledger_.consume(identity);
        switch (consumed.status) {
            case HubEventLedger::ConsumeStatus::CONSUMED:
            case HubEventLedger::ConsumeStatus::ALREADY_CONSUMED:
                setSuccess(result);
                HostProtocol::setNoneValue(result.response.value);
                return result;
            case HubEventLedger::ConsumeStatus::NOT_FOUND:
                setEventFailure(result, HostProtocol::EventResultCode::NOT_FOUND);
                return result;
            case HubEventLedger::ConsumeStatus::STORAGE_FAILURE:
            case HubEventLedger::ConsumeStatus::DEGRADED:
                setEventFailure(result, HostProtocol::EventResultCode::STORAGE_FAILURE);
                return result;
        }
        setEventFailure(result, HostProtocol::EventResultCode::STORAGE_FAILURE);
        return result;
    }

    HubEventLedger::Ledger& ledger_;
    RuntimeState::State* diagnostics_;
};

}  // namespace HostEventService
