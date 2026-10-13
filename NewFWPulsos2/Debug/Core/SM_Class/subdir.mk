################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Core/SM_Class/PuertoSerie.cpp \
../Core/SM_Class/StateMachine.cpp \
../Core/SM_Class/Statemachine_estados.cpp \
../Core/SM_Class/Statemachine_json.cpp \
../Core/SM_Class/Statemachine_senales.cpp \
../Core/SM_Class/Statemachine_utils.cpp 

OBJS += \
./Core/SM_Class/PuertoSerie.o \
./Core/SM_Class/StateMachine.o \
./Core/SM_Class/Statemachine_estados.o \
./Core/SM_Class/Statemachine_json.o \
./Core/SM_Class/Statemachine_senales.o \
./Core/SM_Class/Statemachine_utils.o 

CPP_DEPS += \
./Core/SM_Class/PuertoSerie.d \
./Core/SM_Class/StateMachine.d \
./Core/SM_Class/Statemachine_estados.d \
./Core/SM_Class/Statemachine_json.d \
./Core/SM_Class/Statemachine_senales.d \
./Core/SM_Class/Statemachine_utils.d 


# Each subdirectory must supply rules for building sources it contributes
Core/SM_Class/%.o Core/SM_Class/%.su Core/SM_Class/%.cyclo: ../Core/SM_Class/%.cpp Core/SM_Class/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-SM_Class

clean-Core-2f-SM_Class:
	-$(RM) ./Core/SM_Class/PuertoSerie.cyclo ./Core/SM_Class/PuertoSerie.d ./Core/SM_Class/PuertoSerie.o ./Core/SM_Class/PuertoSerie.su ./Core/SM_Class/StateMachine.cyclo ./Core/SM_Class/StateMachine.d ./Core/SM_Class/StateMachine.o ./Core/SM_Class/StateMachine.su ./Core/SM_Class/Statemachine_estados.cyclo ./Core/SM_Class/Statemachine_estados.d ./Core/SM_Class/Statemachine_estados.o ./Core/SM_Class/Statemachine_estados.su ./Core/SM_Class/Statemachine_json.cyclo ./Core/SM_Class/Statemachine_json.d ./Core/SM_Class/Statemachine_json.o ./Core/SM_Class/Statemachine_json.su ./Core/SM_Class/Statemachine_senales.cyclo ./Core/SM_Class/Statemachine_senales.d ./Core/SM_Class/Statemachine_senales.o ./Core/SM_Class/Statemachine_senales.su ./Core/SM_Class/Statemachine_utils.cyclo ./Core/SM_Class/Statemachine_utils.d ./Core/SM_Class/Statemachine_utils.o ./Core/SM_Class/Statemachine_utils.su

.PHONY: clean-Core-2f-SM_Class

