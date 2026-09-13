# Voltage Command Test Findings

## Test source

The findings below are based on the results recorded in [voltage_commands.jsonl](voltage_commands.jsonl) and the test script in `.python_test/test_voltage_commands.py`.

The script ran 160 trials using random voltage targets between 5 V and 10.99 V. Each trial sent a voltage command, waited before querying the device, compared the returned value with the target, and then waited before starting the next trial.

The command and query used by the script were:

```text
VOLTAGE <value>
VOLTAGE?
```

## Results

| Delay before query | Trials | Passed | Failed | Pass rate |
|---:|---:|---:|---:|---:|
| 0.5 s | 40 | 26 | 14 | 65.0% |
| 1.0 s | 40 | 28 | 12 | 70.0% |
| 5.0 s | 40 | 39 | 1 | 97.5% |
| 10.0 s | 40 | 40 | 0 | 100.0% |
| **Total** | **160** | **133** | **27** | **83.1%** |

The delay after the query was also varied between 0.5, 1, 5, and 10 seconds. The results were:

| Delay after query | Trials | Passed | Failed | Pass rate |
|---:|---:|---:|---:|---:|
| 0.5 s | 40 | 28 | 12 | 70.0% |
| 1.0 s | 40 | 28 | 12 | 70.0% |
| 5.0 s | 40 | 39 | 1 | 97.5% |
| 10.0 s | 40 | 38 | 2 | 95.0% |

## Findings

1. The controller is usually not ready for a query after only 0.5 or 1 second. Together, these delays produced 26 failures out of 80 trials.

## Short-timing rerun and value correlation

A second 160-trial run used a 0.1-second delay before every query. The delay after the query was varied between 0.1, 0.25, 0.5, and 1.0 seconds.

| Result | Count |
|---|---:|
| Correct | 60 |
| Incorrect numeric response | 66 |
| Empty response | 34 |
| Total | 160 |

For this analysis, “previous value” means the target from the preceding command. The log does not contain a query of the voltage before each command, so the actual previous device reading cannot be reconstructed exactly.

### Previous target versus near-zero response

There were 43 numeric responses at or below 0.05 V. None matched the preceding target. The near-zero rate varied strongly with the integer part of the preceding target:

| Previous target range | Near-zero responses | Numeric responses | Rate |
|---:|---:|---:|---:|
| 5.x | 0 | 16 | 0% |
| 6.x | 1 | 32 | 3% |
| 7.x | 6 | 21 | 29% |
| 8.x | 13 | 23 | 57% |
| 9.x | 5 | 12 | 42% |
| 10.x | 18 | 21 | 86% |

The point-biserial correlation between the near-zero indicator and the preceding target was approximately **+0.60**. This is a substantial relationship in this dataset: near-zero responses are much more common after a higher previous target.

The current target was a weaker predictor. Its correlation with the near-zero indicator was approximately **-0.25**, so the near-zero problem is not explained simply by the current requested voltage being high.

### Partial-value pattern

For current targets above 10 V, 24 of 25 numeric responses matched the current target's fractional suffix. Examples include:

```text
target 10.60 -> response 0.60
target 10.10 -> response 0.10
target 10.20 -> response 0.20
```

This points to an incomplete digit update or digit-selection state problem rather than a random measurement error. The controller appears to leave the leading digit at zero or fail to update it while the lower digits are already correct.

### Interpretation

The short-timing run strengthens the timing/state-machine explanation:

1. The returned value is not generally the previous target. Exact previous-target matches were not observed.
2. The previous target still matters: higher preceding values are followed by near-zero responses more often.
3. The most distinctive error is a partially updated current value, especially when the current target is above 10 V.
4. The 0.1-second pre-query delay is too short for reliable command completion. Because every query was made at that delay, this run cannot separate a slow update from a persistent digit-selection bug.

The next useful experiment is to keep the same target and repeat it twice, recording the voltage immediately before the first command and after each command. That will distinguish a stale previous display value from a controller state that fails specifically on repeated commands.

## Previous returned data greater than current target

The updated log was also checked for the specific condition:

```text
previous line data > current line target
```

The first line and rows whose previous `data` was empty were excluded because the comparison is not possible. This left 125 rows with a numeric previous response.

| Condition | Rows | Near-zero responses | Near-zero rate |
|---|---:|---:|---:|
| Previous data > current target | 26 | 21 | 80.8% |
| Previous data <= current target | 99 | 18 | 18.2% |

The near-zero error was therefore about **4.4 times more likely** when the previous returned value was greater than the current target. The corresponding 2x2 odds ratio is approximately **18.9**.

The relationship was present at every tested post-query delay:

| Post-query delay | Previous data > target | Previous data <= target |
|---:|---:|---:|
| 0.1 s | 6/8 (75%) | 4/25 (16%) |
| 0.25 s | 3/4 (75%) | 5/29 (17%) |
| 0.5 s | 7/9 (78%) | 2/21 (10%) |
| 1.0 s | 5/5 (100%) | 7/24 (29%) |

### Conclusion

This is strong evidence that a downward change, where the previous returned value is above the new target, is associated with the near-zero error. It is not proof that the previous value itself causes the error: the test always queried after only 0.1 seconds, and the previous response can itself be a stale or partial value. The likely underlying issue is an incomplete downward digit update or an incorrect controller editing state when reducing the voltage.

To confirm causation, the next test should hold the timing constant and use paired commands such as `9.50 -> 6.50` and `6.50 -> 9.50`, repeating each direction many times. It should record the actual voltage immediately before each command, rather than using only the preceding test result.

## Controlled direction test

The recommended paired test was run with 20 cycles of:

```text
9.50 -> 6.50 -> 9.50
```

The timing was held constant at 0.1 seconds before querying and 0.1 seconds after querying. The test recorded an actual `VOLTAGE?` response immediately before every command and another response after the command. The full records are in [voltage_direction_commands.jsonl](voltage_direction_commands.jsonl), generated by [.python_test/test_voltage_direction.py](.python_test/test_voltage_direction.py).

| Direction | Trials | Passed | Failed | Returned values after command |
|---|---:|---:|---:|---|
| 9.50 -> 6.50 | 20 | 1 | 19 | `0.00` 19 times, `6.50` once |
| 6.50 -> 9.50 | 20 | 20 | 0 | `9.50` 20 times |

Before every downward command, the measured value was `9.50`. Before every upward command, the measured value was `0.00` in 19 cycles and `6.50` in the one cycle where the preceding downward command succeeded. This confirms that the failures are tied to the downward transition, rather than merely to an incorrect assumption about the preceding target.

### Conclusion from the paired test

The condition “previous data is greater than the current target” is a strong marker for the near-zero error, but it is not the root cause by itself. Under identical timing and fixed voltage values:

1. Downward transitions failed in 19 of 20 attempts.
2. Upward transitions succeeded in all 20 attempts.
3. The failed downward transitions consistently produced `0.00`, not the previous `9.50` value.

The likely fault is in the controller's downward digit-rotation or carry handling. The next debugging target should be the `up == false` path in `controller_set_voltage`, especially digit selection and the first digit rotation after entering edit mode.
2. A 5-second delay before querying was nearly reliable, with 39 successful results out of 40.
3. A 10-second delay before querying produced no failures in this dataset.
4. The returned values on failed trials were often incomplete values, such as `0.32`, `0.60`, or `0.89`, when the requested values were `10.32`, `10.60`, or `10.89`. This indicates that the display/controller was still being updated when it was queried.
5. Thirteen failures returned exactly `0.0`, and three returned an empty response. These are consistent with an incomplete display update or a query arriving before a valid capture was available.
6. Two failures returned an unrelated-looking previous or partial value: target `9.99` returned `6.66`, and target `6.10` returned `7.00`.
7. The 5-second group still had one failure: target `5.41` returned an empty response. Therefore, 5 seconds should be treated as a practical minimum observed in this run, not a guaranteed completion time.

## Test limitation

This run tested random voltage changes, but it did not specifically test setting the same voltage twice in succession. A separate regression case should send the same value twice and query after each command, especially with the second command issued immediately after the first.

The script also uses fixed delays rather than waiting for an explicit device-ready response. The firmware currently has no such response, so fixed-delay testing cannot distinguish controller completion from serial timing or query timing.

## Recommended next test

Add repeated-value cases such as:

```text
VOLTAGE 6.10
VOLTAGE?
VOLTAGE 6.10
VOLTAGE?
```

Run this sequence with short and long delays. The result should be compared against the requested value after every command. A useful implementation improvement would be to make the test retry `VOLTAGE?` until the value is valid or a timeout is reached, while still recording the elapsed time needed for the controller to settle.
