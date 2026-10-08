# IoT-Lab-Exp5
Internet of Things Laboratory Experiment #5
## LAB5 Schematic diagram
![Schematic](IoT_Lab5/LAB5_SCHEMATIC.png)
## PART A
Tested 12V DC WATER PUMP, measured its operating voltage, running current, and stall current. Attached and tested to its Relay driver with flyback diode.
## PART B
Tested 12V DC WATER VALVE, and measured and tested in its driver the same way as the Water pump.
## PART C
Combined and connected Lab 4's input sensors, and adjusted its pin configurations. Keypad input can edit irrigation time and once confirmed, irrigation cycle will continue until the set irrigation time is over, where the water pump drives the water from the tank to the valve and into the soil farm. Following Lab 4's farm states.
## PART D
Made an isolated ESP32 WIFI code, tested to connect to WIFI (Phone's hotspot). Connected to open.meteo.com API and get its raw json data as a forecast data.
## PART E
Using the APIs forecast data and displaying it on the LCD and show the current age (how long the data was obtained) of the forecast data. Also, from the forecast data obtained, the system can be paused/suppresed from irrigating if there's a high possibility of rain.
## PART F
Added LED indicators for farm states, and added water level sensor for water tank detection as well as its corresponding buzzer as an empty indicator.
## PART G
Enforced safety limit of actual maximum pump time of 20 minutes, even if the user inputs 20+ minutes, irrigation/pump time will only run with 20 minutes time.
## PART H
All codes were combined from PART G (farm input/output code) and PART E (WiFi & API code), where the system can now connect to the internet and get weather forecast. Fallback to local data is made, where it refers to the local sensors data and the irrigation cycle still operates. Forecast data becomes "stale" status when disconnected or after 3 hours, but will refresh and pull "fresh" data every 10 minutes if connected to the internet.
