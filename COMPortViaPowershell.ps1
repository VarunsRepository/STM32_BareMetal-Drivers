# commmand to identify the COM port first
Get-WmiObject Win32_SerialPort | Select Name, DeviceID

# Expected output
# Name                                              DeviceID
# $----                                              --------
# STMicroelectronics STLink Virtual COM Port (COM9) COM9


# make an object to the com port identified in the above step
 $p = New-Object System.IO.Ports.SerialPort COM9,115200,None,8,one

# Open that port 
$p.Open()

# an infinite loop to read the while while($True)
 while ($true) { $p.ReadLine() }
tick 75
tick 76
tick 77
tick 78
tick 79
tick 80
tick 81
tick 82
tick 83
