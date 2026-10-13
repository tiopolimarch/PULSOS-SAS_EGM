################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Core/JSON/CrearJson.cpp 

OBJS += \
./Core/JSON/CrearJson.o 

CPP_DEPS += \
./Core/JSON/CrearJson.d 


# Each subdirectory must supply rules for building sources it contributes
Core/JSON/%.o Core/JSON/%.su Core/JSON/%.cyclo: ../Core/JSON/%.cpp Core/JSON/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-JSON

clean-Core-2f-JSON:
	-$(RM) ./Core/JSON/CrearJson.cyclo ./Core/JSON/CrearJson.d ./Core/JSON/CrearJson.o ./Core/JSON/CrearJson.su

.PHONY: clean-Core-2f-JSON

