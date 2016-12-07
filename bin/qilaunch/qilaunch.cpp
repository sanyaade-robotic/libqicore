/**
*  Copyright (C) 2012-2016 Aldebaran Robotics
*  See COPYING for the license
*/

#include <csignal>
#include <mutex>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <QtCore/QCoreApplication>
#include <qi/applicationsession.hpp>
#include <qi/jsoncodec.hpp>
#include <qicore/logprovider.hpp>
#include <breakpad/breakpad.h>

static std::vector<std::string> modules;
static std::vector<std::string> objects;
static std::vector<std::string> legacy;
static std::vector<std::string> functions;

std::vector<qi::Future<void>> futures;
std::mutex futuresMutex;

static bool keepRunning = false;
static bool disableBreakpad = false;
static bool disableLogging = false;
static bool qtEventloop = false;

static std::string launcherName;
static int exitStatus = 0;

qiLogCategory("qilaunch");

/// In stage 2, we exit the process right away,
/// regardless the state of the ongoing calls.
void stage2(int)
{
  qiLogInfo() << "Signal received again, forcing exit...";
#ifndef _MSC_VER
  // exit and skip atexit() and static deinit
  // this is C99, not supported by visual...
  _Exit(1);
#else
  exit(1);
#endif
}

/// In stage 1, we request the cancellation every ongoing calls,
/// but we still wait gently for them.
void stage1(int s)
{
  qiLogInfo() << "Signal received, exiting...";
  std::lock_guard<std::mutex> futuresLock(futuresMutex);
  for (auto& future: futures)
  {
    try
    {
      future.cancel();
    }
    catch (const std::exception& e)
    {
      qiLogError() << "Error caught on call cancellation: " << e.what();
    }
  }
  qi::Application::atSignal(&stage2, s);
}

_QI_COMMAND_LINE_OPTIONS(
  "Launcher options",
  ("module,m", value<std::vector<std::string> >(&modules), "Load given module (can be set multiple times) (deprecated, use --object)")
  ("object,o", value<std::vector<std::string> >(&objects), "Load given object (syntax: yourmodule.YourObject) (can be set multiple times)")
  ("legacy,g", value<std::vector<std::string> > (&legacy), "Load given legacy module (can be set multiple times)")
  ("function,f", value<std::vector<std::string> >(&functions), "Call given function (syntax: yourmodule.yourFunction) (can be set multiple times)")
  ("keep-running,k", bool_switch(&keepRunning), "Keep running after the function(s) has(have) returned (implied by -o)")
  ("no-breakpad,b", bool_switch(&disableBreakpad), "Disable breakpad")
  ("no-logging,l", bool_switch(&disableLogging), "Disable remote logging")
  ("qt-eventloop,q", bool_switch(&qtEventloop), "Enable a Qt Eventloop")
  ("name,n", value<std::string>(&launcherName), "Name of the launcher used to prefix logs and breakpad dump files")
)

void stopOnError(qi::Future<qi::AnyValue> fut, const std::string& name)
{
  if (fut.hasValue())
  {
    const auto& value = fut.value();
    if (value.kind() == qi::TypeKind_Void)
      qiLogInfo() << "Call succeeded";
    else
      qiLogInfo() << "Call succeeded with result: " << qi::encodeJSON(value);
  }
  else if (fut.hasError())
    qiLogFatal() << name << " has finished with an error: " << fut.error();
  else if (fut.isCanceled())
    qiLogFatal() << name << " has been canceled";
  else
    assert(false && "future in incoherent state");

  exitStatus = 1;
  qi::Application::stop();
}

// Copied from naoqicore
static void handleQTCoreApplication(int argc, char* argv[])
{
  // Create Main loop QT
  QCoreApplication app(argc, argv);

  bool exited = false;
  while (!exited)
  {
    try
    {
      // Start Main loop QT
      app.exec();
      exited = true;
    }
    catch (std::exception& e)
    {
      qiLogError() << "Uncaught QT main loop exception detected: " << e.what();
    }
    catch (...)
    {
      qiLogError() << "Uncaught QT main loop unknown exception detected.";
    }
  }
}

void runLegacy(const std::string& module, qi::SessionPtr session)
{
  qiLogInfo() << "Loading module " << module << " on " << launcherName << "...";

  std::string fname;
  boost::filesystem::path moduleFullPath(module);
  if (moduleFullPath.is_absolute() &&
      boost::filesystem::exists(moduleFullPath))
  {
    fname = moduleFullPath.make_preferred().string();
  }
  else
  {
    boost::filesystem::path pluginsPath("naoqi");
    pluginsPath /= module;
    fname = qi::path::findLib(pluginsPath.make_preferred().string());
  }

  if (fname.empty())
  {
    std::stringstream ss;
    ss << "Could not find library `" << module << "' in:" << std::endl;
    std::vector<std::string> libPaths = qi::path::libPaths();
    for (auto it = libPaths.begin(); it != libPaths.end(); ++it)
      ss << "\t- " << *it << "/naoqi" << std::endl;
    qiLogError() << ss.str();
    return;
  }

  qiLogInfo() << "Loading " << fname << "...";
  void *handle = qi::os::dlopen(fname.c_str());
  if (!handle)
  {
    qiLogError() << "Could not load library: `" << fname << "':"
                 << qi::os::dlerror();
    return;
  }

  typedef int fn_t(const std::string, qi::SessionPtr);
  fn_t *registerService = (fn_t *) qi::os::dlsym(handle, "_registerService");
  if (!registerService)
  {
    qiLogError() << "Could not find symbol `_registerService' in " << module
                 << ": " << qi::os::dlerror();
    return;
  }

  try
  {
    registerService(launcherName, session);
  }
  catch (const std::exception& e)
  {
    qiLogError() << "Failed to create module: " << e.what();
  }
}

int main(int argc, char** argv)
{
  qi::ApplicationSession app(argc, argv);
  qi::Application::atSignal(&stage1, SIGINT);
  qi::Application::atSignal(&stage1, SIGTERM);
  std::shared_ptr<boost::thread> thread;
  if (qtEventloop)
    thread = std::make_shared<boost::thread>(handleQTCoreApplication, argc, argv);

  if (launcherName.empty())
  {
    qiLogFatal() << "No launcher name, set one with --name or -n";
    return 1;
  }

  if (!modules.empty())
    qiLogWarning() << "--module is deprecated, use --object";

  objects.insert(objects.end(), modules.begin(), modules.end());

  if (app.standAlone())
    keepRunning = true;
  else if (objects.empty() && functions.empty() && legacy.empty())
  {
    qiLogFatal() << "No object to load and no function to call, add one with --object, --legacy or --function";
    return 1;
  }

  if (!disableBreakpad)
  {
#if defined(I_AM_A_ROBOT)
    // allocate so that it lives even after main termination
    auto eh = new BreakpadExceptionHandler("/var/lib/minidump");
#else
    const std::string dumpPaths = qi::path::userWritableDataPath("aldebaran/breakpad", "");
    auto eh = new BreakpadExceptionHandler(dumpPaths);
#endif
    eh->setBuildTag(launcherName + BUILD_TAG);
  }

  try
  {
    qiLogInfo() << "Connection to service directory at " << app.url().str();
    app.startSession();

    try
    {
      if (!disableLogging)
        qi::initializeLogging(app.session(), launcherName);
    }
    catch (std::exception &e)
    {
      qiLogWarning() << "Logs initialization failed with the following error: " << e.what();
    }

    if (!legacy.empty())
    {
      for (const auto& module : legacy)
        runLegacy(module, app.session());
    }

    for (const auto& object : objects)
    {
      qiLogInfo() << "Loading object " << object;
      app.session()->loadService(object);
    }

    {
      std::lock_guard<std::mutex> futuresLock(futuresMutex);
      futures.reserve(functions.size());
      for (const auto& function: functions)
      {
        std::string argsStr;
        auto openArgs = function.find('(');
        auto closeArgs = function.rfind(')');
        if(openArgs != function.npos && closeArgs != function.npos)
        {
          argsStr = function.substr(openArgs + 1, closeArgs - openArgs - 1);
          qiLogDebug() << "Extracting arguments from regex match: " << argsStr;
        }

        auto args = std::make_shared<std::vector<qi::AnyValue>>();
        try
        {
          *args = qi::decodeJSON(std::string("[") + argsStr + "]").as<std::vector<qi::AnyValue>>();
        }
        catch(const std::exception& e)
        {
          qiLogFatal() << "Failed to parse arguments provided to the function call from JSON: "
                       << "[" << argsStr << "]" << ": " << e.what();
        }

        auto extractedFunctionName = function.substr(0, openArgs);
        qiLogInfo() << "Calling function " << extractedFunctionName << "(" << argsStr << ")";
        qi::AnyReferenceVector metaCallArgs;
        for(auto& arg: *args)
          metaCallArgs.push_back(qi::AnyReference::from(arg));
        qi::Future<qi::AnyValue> fut = app.session()->callModule<qi::AnyValue>(extractedFunctionName, metaCallArgs);
        futures.emplace_back(fut.then([args, function](qi::Future<qi::AnyValue> f){ stopOnError(f, function); }));
      }
    }

    if (!objects.empty() || !legacy.empty() || keepRunning)
      app.run();
    else // just wait for the functions to finish
      for (const auto& future : futures)
        future.wait();
  }
  catch (std::exception& e)
  {
    qiLogFatal() << e.what();
    return 1;
  }
  catch (...)
  {
    qiLogFatal() << "Unknown exception";
    return 1;
  }

  if (qtEventloop)
  {
    QCoreApplication::exit();
    thread->interrupt();
    thread->join();
  }

  return exitStatus;
}
