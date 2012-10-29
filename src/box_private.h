/**
* @author Aldebaran Robotics
* Aldebaran Robotics (c) 2012 All Rights Reserved
*/

#pragma once

#ifndef BOX_PRIVATE_H_
# define BOX_PRIVATE_H_

# include <alcommon/albroker.h>

# include <qicore/state_machine.h>
# include <qicore/timeline.h>

class BoxPrivate
{
  friend class Box;

  public:
    BoxPrivate();
    ~BoxPrivate();

    void load();
    void unload();

  private:
    StateMachine*         _stateMachine;
    Timeline*             _timeline;
    std::string           _name;

    /* TODO: remove me */
    boost::shared_ptr<AL::ALBroker> _broker;
};

#endif /* !BOX_PRIVATE_H_ */
