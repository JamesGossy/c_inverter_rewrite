################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
control/%.obj: ../control/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: C2000 Compiler'
	"C:/ti/ccs2041/ccs/tools/compiler/ti-cgt-c2000_22.6.3.LTS/bin/cl2000" -v28 -ml -mt --cla_support=cla1 --float_support=fpu32 --tmu_support=tmu0 --vcu_support=vcu2 -Ooff --include_path="C:/Users/hames/OneDrive/Documents/Engineering/Projects/c_inverter_rewrite/Driverlib Empty CPU1 Example CCS Project" --include_path="C:/Users/hames/OneDrive/Documents/Engineering/Projects/c_inverter_rewrite/Driverlib Empty CPU1 Example CCS Project/device" --include_path="C:/ti/C2000Ware_6_00_01_00/driverlib/f2837xd/driverlib/" --include_path="C:/ti/ccs2041/ccs/tools/compiler/ti-cgt-c2000_22.6.3.LTS/include" --define=DEBUG --define=CPU1 --define=_LAUNCHXL_F28379D --diag_suppress=10063 --diag_warning=225 --diag_wrap=off --display_error_number --abi=eabi --preproc_with_compile --preproc_dependency="control/$(basename $(<F)).d_raw" --include_path="C:/Users/hames/OneDrive/Documents/Engineering/Projects/c_inverter_rewrite/Driverlib Empty CPU1 Example CCS Project/CPU1_RAM/syscfg" --obj_directory="control" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


