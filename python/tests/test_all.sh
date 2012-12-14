#!/bin/sh

echo " -----------------------------------------------------"
echo "|            LibQicore - Behavior Tester              |"
echo " -----------------------------------------------------"
echo "Starting..."

forceContinue=$1

logFile=test_result.log
errorFile=test_result.err

rm -Rf objects $logFile
fileList=`find ../../../../behaviors/ -name 'behavior.xar' -printf "%p:"`
fileNumber="${fileList//[^:]/}"
fileNumber=${#fileNumber}

echo "$fileNumber file(s) found."
echo "Begin testing..."
echo ""

function printError
{
  echo -e "=====> [\e[0;31mFAIL\e[0m] : $1"
}

function printWarn
{
  echo -e "=====> [\e[0;33mWARN\e[0m] : $1"
}

function printPass
{
  echo -e "=====> [\e[4;32mPASS\e[0m]"
}

function printNull
{
  echo -e "=====> [NULL]"
}

count=0
errorConversionCount=0
errorRunTimeCount=0
timeoutCount=0
passCount=0
IFS=:
for str in $fileList
do
  count=$((count + 1))
  echo "[$count/$fileNumber] : $str"
  python2 ../py/xar_converter.py "$str" >> $logFile 2> $errorFile
  result=$?
  if [ $result -ne 0 ]
  then
    if [ $result -eq 12 ]
    then
      printNull "Incorrect format, abort test"
      echo ""
      continue
    fi
    errorConversionCount=$((errorConversionCount + 1))
    printError "Conversion fail"
    echo `cat $errorFile`
    echo ""
    if [ "$forceContinue" != "--continue" ]
    then
      exit
    fi
    continue
  fi
  timeout 60 python2 objects/main.py 127.0.0.1 9559 >> $logFile 2> $errorFile
  result=$?
  if [ $result -ne 0 ]
  then
    if [ $result -eq 124 ]
    then
      timeoutCount=$((timeoutCount + 1))
      printWarn "TimeOut"
      echo ""
    else
      errorRunTimeCount=$((errorRunTimeCount + 1))
      printError "RunTime Error"
      echo `cat $errorFile`
      echo ""
      if [ "$forceContinue" != "--continue" ]
      then
        exit
      fi
    fi
  else
    passCount=$((passCount + 1))
    printPass
    echo ""
  fi
  rm -Rf objects
done

echo ""
echo "-----------------------------------------------------"
echo "Results :"
echo "-> Pass : $passCount"
echo "-> TimeOut: $timeoutCount"
echo "-> Conversion Error: $errorConversionCount"
echo "-> RunTime Error: $errorRunTimeCount"
echo "Total file(s) tested: $count"