# About Test Cases

## Directory Structure

+ `cases`: Test cases. Each file corresponds to a test case.
+ `probe`: Generic probes that run inside the sandbox.
+ `utils`: Common utilities.

## Test Workflow

1. The test case launches and executes some initialization code.
2. The test case executes a probe. The probe automatically runs in an independent sandbox, executes the corresponding code, and returns the result.
3. The test case verifies the probe's execution result.

## About Probes

A Probe is a piece of independent code integrated into the test case program that can be launched with special arguments. When a test case needs to execute a Probe, the detailed workflow is as follows:

1. The Probe packages its arguments into JSON format and serializes them as a string.
2. The Probe generates a configuration file and launches a Loader process, passing the configuration file to the Loader. This process is named `Loader (Native)`.
3. `Loader (Native)` relaunches itself via Microsoft/Detours and injects the Sandbox DLL into this process. This process is named `Loader (Sandboxed)`.
4. `Loader (Sandboxed)` launches the test case subprocess with special arguments, causing the subprocess to execute the Probe code with the previously packaged arguments.
5. After the Probe code finishes execution, it packages the result as a JSON object and sends it back to the test case program.
6. The test case checks whether the result returned by the Probe is correct.

In the above process, since the Probe code runs in a sandboxed environment, it can verify that the sandbox's isolation functionality works correctly.

Note:
For non-common probes, it should be defined in the self test file.
