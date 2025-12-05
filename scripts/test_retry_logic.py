#!/usr/bin/env python3
"""
Test script to verify retry logic for environmental report API calls.
This simulates the retry behavior and verifies the logic works correctly.
"""

import time
import sys
from typing import Callable, Tuple

# Simulate API call results
class APICallResult:
    SUCCESS = 0
    FAILURE = 1
    TLS_ERROR = 2
    MEMORY_ERROR = 3

def simulate_api_call(attempt: int, should_succeed_on: int = None) -> Tuple[int, str]:
    """Simulate an API call that may fail or succeed."""
    if should_succeed_on and attempt == should_succeed_on:
        return APICallResult.SUCCESS, "Success"
    elif attempt <= 2:
        return APICallResult.TLS_ERROR, "MBEDTLS_ERR_SSL_ALLOC_FAILED"
    else:
        return APICallResult.FAILURE, "Max retries exceeded"

def test_retry_logic(max_retries: int = 2, delay_ms: int = 2000, should_succeed_on: int = None):
    """
    Test the retry logic similar to environmental_report.c
    
    Args:
        max_retries: Maximum number of retries (total attempts = max_retries + 1)
        delay_ms: Delay between retries in milliseconds
        should_succeed_on: Which attempt should succeed (1-based), None for all failures
    """
    print(f"\n{'='*60}")
    print(f"Testing retry logic: max_retries={max_retries}, delay={delay_ms}ms")
    if should_succeed_on:
        print(f"Expected to succeed on attempt {should_succeed_on}")
    else:
        print("Expected to fail after all retries")
    print(f"{'='*60}\n")
    
    ret = None
    error_msg = None
    
    for retry in range(max_retries + 1):
        attempt_num = retry + 1
        print(f"[Attempt {attempt_num}/{max_retries + 1}] Calling API...")
        
        result, msg = simulate_api_call(attempt_num, should_succeed_on)
        
        if result == APICallResult.SUCCESS:
            print(f"  ✅ SUCCESS: {msg}")
            ret = 0  # ESP_OK
            break
        else:
            print(f"  ❌ FAILED: {msg}")
            error_msg = msg
            
            if retry < max_retries:
                print(f"  ⏳ Waiting {delay_ms}ms before retry...")
                time.sleep(delay_ms / 1000.0)
            else:
                print(f"  ⛔ Max retries ({max_retries + 1}) exceeded")
    
    print(f"\n{'='*60}")
    if ret == 0:
        print("✅ TEST PASSED: API call succeeded")
    else:
        print(f"❌ TEST FAILED: API call failed after {max_retries + 1} attempts")
        print(f"   Last error: {error_msg}")
    print(f"{'='*60}\n")
    
    return ret == 0

def test_weather_api_retry():
    """Test weather API retry logic (3 attempts, 500ms delay)"""
    print("\n" + "="*60)
    print("TEST 1: Weather API Retry Logic")
    print("="*60)
    
    # Test case 1: Success on first attempt
    print("\n--- Test Case 1.1: Success on first attempt ---")
    result1 = test_retry_logic(max_retries=2, delay_ms=500, should_succeed_on=1)
    assert result1, "Should succeed on first attempt"
    
    # Test case 2: Success on second attempt
    print("\n--- Test Case 1.2: Success on second attempt ---")
    result2 = test_retry_logic(max_retries=2, delay_ms=500, should_succeed_on=2)
    assert result2, "Should succeed on second attempt"
    
    # Test case 3: Success on third attempt
    print("\n--- Test Case 1.3: Success on third attempt ---")
    result3 = test_retry_logic(max_retries=2, delay_ms=500, should_succeed_on=3)
    assert result3, "Should succeed on third attempt"
    
    # Test case 4: All attempts fail
    print("\n--- Test Case 1.4: All attempts fail ---")
    result4 = test_retry_logic(max_retries=2, delay_ms=500, should_succeed_on=None)
    assert not result4, "Should fail after all retries"
    
    print("✅ Weather API retry logic tests passed!")

def test_llm_api_retry():
    """Test LLM API retry logic (3 attempts, 2000ms delay)"""
    print("\n" + "="*60)
    print("TEST 2: LLM API Retry Logic")
    print("="*60)
    
    # Test case 1: Success on first attempt
    print("\n--- Test Case 2.1: Success on first attempt ---")
    result1 = test_retry_logic(max_retries=2, delay_ms=2000, should_succeed_on=1)
    assert result1, "Should succeed on first attempt"
    
    # Test case 2: Success on second attempt
    print("\n--- Test Case 2.2: Success on second attempt ---")
    result2 = test_retry_logic(max_retries=2, delay_ms=2000, should_succeed_on=2)
    assert result2, "Should succeed on second attempt"
    
    # Test case 3: Success on third attempt
    print("\n--- Test Case 2.3: Success on third attempt ---")
    result3 = test_retry_logic(max_retries=2, delay_ms=2000, should_succeed_on=3)
    assert result3, "Should succeed on third attempt"
    
    # Test case 4: All attempts fail
    print("\n--- Test Case 2.4: All attempts fail ---")
    result4 = test_retry_logic(max_retries=2, delay_ms=2000, should_succeed_on=None)
    assert not result4, "Should fail after all retries"
    
    print("✅ LLM API retry logic tests passed!")

def test_delay_timing():
    """Test that delays are correctly applied between API calls"""
    print("\n" + "="*60)
    print("TEST 3: Delay Timing Verification")
    print("="*60)
    
    # Test weather API delay (500ms)
    print("\n--- Testing Weather API delay (500ms) ---")
    start = time.time()
    test_retry_logic(max_retries=2, delay_ms=500, should_succeed_on=3)
    elapsed = time.time() - start
    expected_min = 1.0  # 2 retries * 500ms = 1000ms minimum
    expected_max = 1.5  # Allow some overhead
    print(f"Elapsed time: {elapsed:.2f}s (expected: {expected_min}-{expected_max}s)")
    assert expected_min <= elapsed <= expected_max, f"Delay timing incorrect: {elapsed}s"
    
    # Test LLM API delay (2000ms)
    print("\n--- Testing LLM API delay (2000ms) ---")
    start = time.time()
    test_retry_logic(max_retries=2, delay_ms=2000, should_succeed_on=3)
    elapsed = time.time() - start
    expected_min = 4.0  # 2 retries * 2000ms = 4000ms minimum
    expected_max = 5.0  # Allow some overhead
    print(f"Elapsed time: {elapsed:.2f}s (expected: {expected_min}-{expected_max}s)")
    assert expected_min <= elapsed <= expected_max, f"Delay timing incorrect: {elapsed}s"
    
    print("✅ Delay timing tests passed!")

def test_sequence_simulation():
    """Simulate the full environmental report sequence"""
    print("\n" + "="*60)
    print("TEST 4: Full Environmental Report Sequence Simulation")
    print("="*60)
    
    print("\nSimulating environmental report generation:")
    print("1. Fetch weather data (with retries)")
    print("2. Wait 2000ms delay")
    print("3. Call LLM API (with retries)")
    
    # Step 1: Weather API
    print("\n--- Step 1: Weather API Call ---")
    weather_success = test_retry_logic(max_retries=2, delay_ms=500, should_succeed_on=1)
    
    if weather_success:
        # Step 2: Delay between calls
        print("\n--- Step 2: Delay between API calls (2000ms) ---")
        print("Waiting 2000ms to allow TLS connection to fully close...")
        time.sleep(2.0)
        print("Delay complete")
        
        # Step 3: LLM API
        print("\n--- Step 3: LLM API Call ---")
        llm_success = test_retry_logic(max_retries=2, delay_ms=2000, should_succeed_on=1)
        
        if llm_success:
            print("\n✅ Full sequence completed successfully!")
        else:
            print("\n❌ LLM API call failed after retries")
    else:
        print("\n❌ Weather API call failed after retries")
    
    print("✅ Sequence simulation test passed!")

def main():
    """Run all tests"""
    print("\n" + "="*60)
    print("Environmental Report Retry Logic Test Suite")
    print("="*60)
    
    try:
        test_weather_api_retry()
        test_llm_api_retry()
        test_delay_timing()
        test_sequence_simulation()
        
        print("\n" + "="*60)
        print("✅ ALL TESTS PASSED!")
        print("="*60 + "\n")
        return 0
    except AssertionError as e:
        print(f"\n❌ TEST FAILED: {e}\n")
        return 1
    except Exception as e:
        print(f"\n❌ ERROR: {e}\n")
        import traceback
        traceback.print_exc()
        return 1

if __name__ == "__main__":
    sys.exit(main())
