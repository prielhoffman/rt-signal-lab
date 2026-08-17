# Real-Time Signal Lab



A real-time embedded system that generates an analog waveform, samples it, processes the acquired data, and validates the results against Python and MATLAB.



> **Current status:** Architecture and MCU selection completed. Firmware implementation and hardware validation have not started yet.



## Motivation



During my Communication Systems Engineering degree and my work on radar receiver simulations at ELTA, I worked with signal processing mainly through MATLAB models and simulations.



I chose this project to explore how the same signal-processing principles behave when implemented on real hardware under practical constraints such as sampling rate, quantization, processing deadlines, limited memory, and communication bandwidth.



The goal is to generate a real analog signal, sample it, process the acquired data in real time, and validate the embedded results against MATLAB as a golden reference. This project connects my signal-processing background with deterministic embedded implementation and hardware/software integration.



## Planned System Architecture



`Digital waveform samples → DAC → series protection resistor → ADC → Circular DMA → real-time processing → UART → Python/MATLAB`



The first version uses an analog loopback connection between the internal DAC and ADC. No external signal generator, external DAC, oscilloscope, or analog RC filter is required.



## MCU Selection



Two available development boards were evaluated:



| Feature | STM32G071RB | STM32F411RE |

|---|---|---|

| CPU | Cortex-M0+, up to 64 MHz | Cortex-M4F, up to 100 MHz |

| FPU and DSP instructions | No | Yes |

| Flash | 128 KB | 512 KB |

| RAM | 36 KB | 128 KB |

| Internal DAC | Two 12-bit output channels | None |

| ADC | 12-bit, up to 2.5 MSPS | 12-bit, up to 2.4 MSPS |

| DMA | 7 channels with DMAMUX | 16 DMA streams |

| Complete internal analog loopback | Supported | Requires external DAC |



Although the STM32F411RE provides more processing power, RAM, Flash, an FPU, and DSP instructions, it does not include an internal DAC. It therefore cannot generate a real analog waveform without an external DAC or signal generator.



The **STM32G071RB** was selected because it integrates the DAC, ADC, timers, and DMA required to implement the complete signal path on a single board.



## Minimum Viable Product



The first end-to-end version will:



- Generate an initial 100 Hz sine wave using the internal DAC.

- Use a hardware timer to define a 20 kS/s update and sampling rate.

- Sample the analog loopback signal using the internal ADC.

- Transfer ADC samples using circular DMA.

- Use one 1024-sample circular buffer divided into two 512-sample halves.

- Use Half-Transfer and Transfer-Complete callbacks only to set processing flags.

- Perform signal processing outside interrupt context.

- Calculate minimum, maximum, average, peak-to-peak, RMS, and frequency.

- Detect missed processing deadlines and buffer overruns.

- Send measurements and selected sample blocks through UART.

- Display live samples and save CSV files using Python.

- Validate the same measured samples using MATLAB as a golden reference.



The MVP does not initially include FFT, FreeRTOS, LCD, MicroSD, RTC, an analog RC filter, or a binary UART protocol.



## Initial Timing and Memory Budget



Initial sampling-rate target:



`Fs = 20,000 samples/second`



For a 100 Hz waveform:



`Samples per cycle = 20,000 / 100 = 200`



ADC buffer memory:



`1024 samples × 2 bytes = 2048 bytes`



Available processing time for each 512-sample half-buffer:



`512 / 20,000 = 25.6 ms`



At a 64 MHz CPU clock, this corresponds to approximately 1.64 million CPU cycles per half-buffer.



These values are initial design targets. Actual execution time, memory usage, and deadline compliance will be measured before any real-time performance claim is made.



## Planned Core Resources



| Function | Peripheral | MCU Pin | Nucleo Pin |

|---|---|---|---|

| Analog output | DAC1 Channel 1 | PA4 / DAC_OUT1 | Arduino A2 |

| Analog input | ADC1 IN0 | PA0 | Arduino A0 |

| DAC and ADC timing | TIM6 TRGO | Internal connection | No external pin |

| PC communication | USART2 | PA2 / PA3 | ST-LINK Virtual COM Port |

| Digital timing marker | GPIO | PA8 | Arduino D7 |



`TIM6_TRGO` is planned as a common hardware trigger for the DAC and ADC. A possible fixed delay between the DAC update and ADC acquisition will be characterized during the synchronization stage.



## Buffering Model



The acquisition system will use one circular ADC buffer divided into two equal halves:



- Samples 0–511: first half.

- Samples 512–1023: second half.



While DMA fills one half, the application processes the other half. This is a two-half circular-buffer design, not two separate memory arrays.



The interrupt callbacks will remain short and will only update flags or counters. Calculations, UART transmission, and future storage operations will run outside interrupt context.



## DSP Scope



The STM32G071RB is not a dedicated DSP processor and does not include an FPU or specialized DSP instructions. However, it is suitable for the planned time-domain measurements and frequency estimation.



A small fixed-point FFT may be evaluated later, but it will only be included after measuring:



- RAM consumption.

- Execution time.

- Numerical accuracy.

- Effect on acquisition deadlines.

- Overrun behavior.



## Validation Strategy



The same ADC samples will be used by all validation layers:



1. The STM32 will calculate the real-time metrics.

2. Python will receive, display, and save the samples.

3. MATLAB will load the saved samples and independently calculate RMS, frequency, peak-to-peak, and FFT results.

4. MATLAB will calculate the percentage error and produce PASS/FAIL results.



No feature will be marked as completed until it has been tested and documented.



## Repository Structure



```text

rt-signal-lab/

├── firmware/   STM32CubeIDE firmware

├── python/     PC control, live plotting, and CSV logging

├── matlab/     Golden-reference validation scripts

├── docs/       Architecture, wiring, and test documentation

├── results/    Selected measurement results and plots

└── README.md   Project overview and progress

```



## Development Roadmap



- [x] Define the project motivation and architecture.

- [x] Compare STM32G071RB and STM32F411RE.

- [x] Select STM32G071RB and define the MVP.

- [ ] Create, build, and flash the base STM32CubeIDE project.

- [ ] Verify UART communication through ST-LINK.

- [ ] Generate and measure a fixed DAC voltage.

- [ ] Implement basic DAC-to-ADC loopback.

- [ ] Add hardware timer triggering.

- [ ] Add normal and circular ADC DMA.

- [ ] Implement half-buffer processing and overrun detection.

- [ ] Calculate time-domain signal metrics.

- [ ] Generate periodic waveforms using DAC, timer, and DMA.

- [ ] Implement frequency estimation.

- [ ] Build the Python control and visualization application.

- [ ] Validate the measured data using MATLAB.

- [ ] Evaluate optional FFT and system extensions.



## Official References



- [STM32G071RB product page](https://www.st.com/en/microcontrollers-microprocessors/stm32g071rb.html)

- [STM32G071RB datasheet](https://www.st.com/resource/en/datasheet/stm32g071c8.pdf)

- [STM32G0x1 reference manual – RM0444](https://www.st.com/resource/en/reference_manual/dm00371828.pdf)

- [NUCLEO-G071RB product page](https://www.st.com/en/evaluation-tools/nucleo-g071rb.html)

- [NUCLEO-G071RB user manual – UM2324](https://www.st.com/resource/en/user_manual/um2324-stm32-nucleo64-boards-mb1360-stmicroelectronics.pdf)

- [NUCLEO-G071RB schematic](https://www.st.com/resource/en/schematic_pack/mb1360-g071rb-c02_schematic.pdf)

- [STM32F411RE product page](https://www.st.com/en/microcontrollers-microprocessors/stm32f411re.html)

- [STM32F411RE datasheet](https://www.st.com/resource/en/datasheet/stm32f411ce.pdf)

