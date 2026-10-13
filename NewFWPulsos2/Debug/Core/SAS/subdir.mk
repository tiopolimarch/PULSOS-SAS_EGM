################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Core/SAS/SasEgm.cpp \
../Core/SAS/SasEgmStm32.cpp \
../Core/SAS/SasEgm_aft.cpp \
../Core/SAS/Serial9BitStm32.cpp 

OBJS += \
./Core/SAS/SasEgm.o \
./Core/SAS/SasEgmStm32.o \
./Core/SAS/SasEgm_aft.o \
./Core/SAS/Serial9BitStm32.o 

CPP_DEPS += \
./Core/SAS/SasEgm.d \
./Core/SAS/SasEgmStm32.d \
./Core/SAS/SasEgm_aft.d \
./Core/SAS/Serial9BitStm32.d 


# Each subdirectory must supply rules for building sources it contributes
Core/SAS/%.o Core/SAS/%.su Core/SAS/%.cyclo: ../Core/SAS/%.cpp Core/SAS/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-SAS

clean-Core-2f-SAS:
	-$(RM) ./Core/SAS/SasEgm.cyclo ./Core/SAS/SasEgm.d ./Core/SAS/SasEgm.o ./Core/SAS/SasEgm.su ./Core/SAS/SasEgmStm32.cyclo ./Core/SAS/SasEgmStm32.d ./Core/SAS/SasEgmStm32.o ./Core/SAS/SasEgmStm32.su ./Core/SAS/SasEgm_aft.cyclo ./Core/SAS/SasEgm_aft.d ./Core/SAS/SasEgm_aft.o ./Core/SAS/SasEgm_aft.su ./Core/SAS/Serial9BitStm32.cyclo ./Core/SAS/Serial9BitStm32.d ./Core/SAS/Serial9BitStm32.o ./Core/SAS/Serial9BitStm32.su

.PHONY: clean-Core-2f-SAS

