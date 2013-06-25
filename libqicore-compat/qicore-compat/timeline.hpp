/**
* @author Aldebaran Robotics
* Aldebaran Robotics (c) 2012 All Rights Reserved
*/

#pragma once

#ifndef TIMELINE_H_
# define TIMELINE_H_

#include <string>

#include <boost/shared_ptr.hpp>

#include <qicore-compat/api.hpp>
#include <qitype/anyobject.hpp>

namespace AL
{
  class ALBroker;
}

namespace qi
{

class TimelinePrivate;
class AnimationModel;

/// Timeline class
class QICORECOMPAT_API Timeline
{
  public:
    /// Constructor, take a ALBroker to speak with the outside world
    Timeline(AnyObject memory, AnyObject motion);
    ~Timeline();

    /// Play the timeline
    void play(void);
    /// Pause the timeline
    void pause(void);
    /// Stop the timeline
    void stop(void);

    /// Goto to a frame
    void goTo(const int &pFrame);

    /// Return the number of frames in the timeline
    int getSize() const;
    /// Return FPS
    int getFPS() const;
    /// Change the FPS
    void setFPS(const int fps);
    /// Change the animation
    void setAnimation(boost::shared_ptr<AnimationModel> anim);

    /// Wait untile the timeline execution is completed
    void waitForTimelineCompletion();

  private:
    TimelinePrivate* _p;
};

}

#endif /* !TIMELINE_H_ */