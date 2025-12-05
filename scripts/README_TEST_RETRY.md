# Retry Logic Test Suite

This test suite verifies the retry logic implementation for the environmental report API calls.

## Overview

The environmental report feature makes two API calls:
1. **Weather API** - Fetches weather and air quality data (with retry logic)
2. **LLM API** - Generates natural language summary (with retry logic)

Both APIs implement retry logic to handle transient network and TLS errors.

## Test Script

### Location
`scripts/test_retry_logic.py`

### Running the Tests

```bash
# From project root
python3 scripts/test_retry_logic.py

# Or make it executable and run directly
chmod +x scripts/test_retry_logic.py
./scripts/test_retry_logic.py
```

## Test Coverage

### Test 1: Weather API Retry Logic
- ✅ Success on first attempt
- ✅ Success on second attempt (after 500ms delay)
- ✅ Success on third attempt (after 2x 500ms delays)
- ✅ Failure after all 3 attempts

**Configuration:**
- Max retries: 2 (total 3 attempts)
- Delay between retries: 500ms

### Test 2: LLM API Retry Logic
- ✅ Success on first attempt
- ✅ Success on second attempt (after 2000ms delay)
- ✅ Success on third attempt (after 2x 2000ms delays)
- ✅ Failure after all 3 attempts

**Configuration:**
- Max retries: 2 (total 3 attempts)
- Delay between retries: 2000ms

### Test 3: Delay Timing Verification
- ✅ Verifies weather API delays are ~500ms
- ✅ Verifies LLM API delays are ~2000ms

### Test 4: Full Sequence Simulation
- ✅ Simulates complete environmental report flow:
  1. Weather API call (with retries)
  2. 2000ms delay between calls
  3. LLM API call (with retries)

## Implementation Details

### Weather API Retry Logic
```c
const int WEATHER_MAX_RETRIES = 2;
for (int retry = 0; retry <= WEATHER_MAX_RETRIES; retry++) {
    fetch_ret = environmental_report_fetch_weather_data(...);
    if (fetch_ret == ESP_OK) break;
    if (retry < WEATHER_MAX_RETRIES) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

### LLM API Retry Logic
```c
// 2000ms delay after weather API completes
vTaskDelay(pdMS_TO_TICKS(2000));

const int LLM_MAX_RETRIES = 2;
for (int retry = 0; retry <= LLM_MAX_RETRIES; retry++) {
    ret = gemini_llm(prompt, llm_response, sizeof(llm_response));
    if (ret == ESP_OK) break;
    if (retry < LLM_MAX_RETRIES) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

## Expected Behavior

### Success Cases
- API calls succeed on first attempt → No retries needed
- API calls succeed on retry → System recovers from transient errors
- All retries exhausted → System gracefully handles persistent failures

### Error Handling
- TLS errors (`MBEDTLS_ERR_SSL_ALLOC_FAILED`) → Retry with delay
- Network errors → Retry with delay
- Persistent failures → Log error and continue (non-blocking)

## Memory Considerations

The retry logic includes delays to allow TLS connections to fully close and release internal RAM:
- **Weather API**: 500ms delay (faster, less critical)
- **LLM API**: 2000ms delay (longer, allows TLS resources to fully release)
- **Between calls**: 2000ms delay (prevents concurrent TLS connection conflicts)

## Troubleshooting

If tests fail:
1. Check Python version (requires Python 3.6+)
2. Verify script has execute permissions
3. Check for syntax errors in the test script

If retry logic doesn't work on device:
1. Check serial monitor for retry attempt logs
2. Verify delays are being applied (check timestamps)
3. Check for memory errors that might prevent retries
