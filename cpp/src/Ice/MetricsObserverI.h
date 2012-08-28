// **********************************************************************
//
// Copyright (c) 2003-2012 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef ICE_METRICSOBSERVER_I_H
#define ICE_METRICSOBSERVER_I_H

#include <IceUtil/StopWatch.h>

#include <Ice/Observer.h>
#include <Ice/Metrics.h>

#include <Ice/MetricsAdminI.h>
#include <Ice/MetricsFunctional.h>

namespace IceMX
{

class MetricsHelper
{
public:

    virtual std::string operator()(const std::string&) const = 0;
};

class Updater : public IceUtil::Shared
{
public:

    virtual void update() = 0;
};
typedef IceUtil::Handle<Updater> UpdaterPtr;

template<typename T> class MetricsHelperT : public MetricsHelper
{
public:

    virtual void initMetrics(const IceInternal::Handle<T>&) const
    {
        // To be overriden in specialization to initialize state attributes
    }

protected:

    template<typename Helper> class AttributeResolverT
    {
        class Resolver
        {
        public:
            virtual ~Resolver() { }
            virtual std::string operator()(const Helper* h) const = 0;
        };

    public:

        ~AttributeResolverT()
        {
            for(typename std::map<std::string, Resolver*>::iterator p = _attributes.begin(); p != _attributes.end();++p)
            {
                delete p->second;
            }
        }

        std::string operator()(const Helper* helper, const std::string& attribute) const
        {
            typename std::map<std::string, Resolver*>::const_iterator p = _attributes.find(attribute);
            if(p == _attributes.end())
            {
                if(attribute == "none")
                {
                    return "";
                }
                return "unknown";
            }
            return (*p->second)(helper);
        }

        template<typename Y> void
        add(const std::string& name, Y Helper::*member)
        {
            _attributes.insert(make_pair(name,  new HelperMemberResolver<Y>(member)));
        }

        template<typename Y> void
        add(const std::string& name, Y (Helper::*memberFn)() const)
        {
            _attributes.insert(make_pair(name,  new HelperMemberFunctionResolver<Y>(memberFn)));
        }

        template<typename I, typename O, typename Y> void
        add(const std::string& name, O (Helper::*getFn)() const, Y I::*member)
        {
            _attributes.insert(make_pair(name,  new MemberResolver<I, O, Y>(getFn, member)));
        }

        template<typename I, typename O, typename Y> void
        add(const std::string& name, O (Helper::*getFn)() const, Y (I::*memberFn)() const)
        {
            _attributes.insert(make_pair(name,  new MemberFunctionResolver<I, O, Y>(getFn, memberFn)));
        }

    private:
            
        template<typename Y> class HelperMemberResolver : public Resolver
        {
        public:

            HelperMemberResolver(Y Helper::*member) : _member(member)
            {
            }

            virtual std::string operator()(const Helper* r) const
            {
                return toString(r->*_member);
            }

        private:

            Y Helper::*_member;
        };

        template<typename Y> class HelperMemberFunctionResolver : public Resolver
        {
        public:

            HelperMemberFunctionResolver(Y (Helper::*memberFn)() const) : _memberFn(memberFn)
            {
            }

            virtual std::string operator()(const Helper* r) const
            {
                return toString((r->*_memberFn)());
            }

        private:

            Y (Helper::*_memberFn)() const;
        };


        template<typename I, typename O, typename Y> class MemberResolver : public Resolver
        {
        public:

            MemberResolver(O (Helper::*getFn)() const, Y I::*member) : _getFn(getFn), _member(member)
            {
            }

            virtual std::string operator()(const Helper* r) const
            {
                O o = (r->*_getFn)();
                I* v = dynamic_cast<I*>(ReferenceWrapper<O>::get(o));
                if(v)
                {
                    return toString(v->*_member);
                }
                else
                {
                    return "unknown";
                }
            }

        private:

            O (Helper::*_getFn)() const;
            Y I::*_member;
        };

        template<typename I, typename O, typename Y> class MemberFunctionResolver : public Resolver
        {
        public:

            MemberFunctionResolver(O (Helper::*getFn)() const, Y (I::*memberFn)() const) :
                _getFn(getFn), _memberFn(memberFn)
            {
            }

            virtual std::string operator()(const Helper* r) const
            {
                O o = (r->*_getFn)();
                I* v = dynamic_cast<I*>(ReferenceWrapper<O>::get(o));
                if(v)
                {
                    return toString((v->*_memberFn)());
                }
                else
                {
                    return "unknown";
                }
            }

        private:

            O (Helper::*_getFn)() const;
            Y (I::*_memberFn)() const;
        };

        template<typename I> static std::string
        toString(const I& v)
        {
            std::ostringstream os;
            os << v;
            return os.str();
        }

        static std::string
        toString(const std::string& s)
        {
            return s;
        }

        static std::string
        toString(bool v)
        {
            return v ? "true" : "false";
        }

        std::map<std::string, Resolver*> _attributes;
    };
};

template<typename T> class UpdaterT : public Updater
{
public:
    
    UpdaterT(T* updater, void (T::*fn)()) : 
        _updater(updater), _fn(fn)
    {
    }
        
    virtual void update()
    {
        (_updater.get()->*_fn)();
    }
    
private: 
    
    const IceUtil::Handle<T> _updater;
    void (T::*_fn)();
};

class ObserverI;

template<class MetricsType> class ObserverT : virtual public Ice::Instrumentation::Observer
{
public:

    typedef MetricsType Type;
    typedef IceInternal::Handle<MetricsType> PtrType;
    typedef std::vector<std::pair<PtrType, MetricsMapI::EntryPtr> > SeqType;

    ObserverT()
    {
    }

    virtual void 
    attach()
    {
        _watch.start();
    }

    virtual void 
    detach()
    {
        Ice::Long lifetime = _watch.stop();
        for(typename SeqType::const_iterator p = _objects.begin(); p != _objects.end(); ++p)
        {
            p->second->detach(lifetime);
        }
    }

    virtual void
    failed(const std::string& exceptionName)
    {
        for(typename SeqType::const_iterator p = _objects.begin(); p != _objects.end(); ++p)
        {
            p->second->failed(exceptionName);
        }
    }

    template<typename Function> void
    forEach(const Function& func)
    {
        for(typename SeqType::const_iterator p = _objects.begin(); p != _objects.end(); ++p)
        {
            p->second->execute(func, p->first);
        }
    }
    
    void
    update(const MetricsHelperT<MetricsType>& helper, const std::vector<MetricsMapI::EntryPtr>& objects)
    {
        std::vector<MetricsMapI::EntryPtr>::const_iterator p = objects.begin();
        typename SeqType::iterator q = _objects.begin();
        while(p != objects.end())
        {
            if(q == _objects.end() || *p < q->second) // New metrics object
            {
                q = _objects.insert(q, std::make_pair((*p)->attach(helper), *p));
                ++p;
                ++q;
            }
            else if(*p == q->second) // Same metrics object
            {
                ++p;
                ++q;
            }
            else // Removed metrics object
            {
                q = _objects.erase(q);
            }
        }
    }

    template<typename ObserverImpl, typename ObserverMetricsType> IceInternal::Handle<ObserverImpl>
    getObserver(const std::string& mapName, const MetricsHelperT<ObserverMetricsType>& helper)
    {
        std::vector<MetricsMapI::EntryPtr> metricsObjects;
        for(typename SeqType::const_iterator p = _objects.begin(); p != _objects.end(); ++p)
        {
            MetricsMapI::EntryPtr e = p->second->getMatching(mapName, helper);
            if(e)
            {
                metricsObjects.push_back(e);
            }
        }

        IceInternal::Handle<ObserverImpl> obsv = new ObserverImpl();
        obsv->update(helper, metricsObjects);
        return obsv;
    }
    
private:

    SeqType _objects;
    IceUtilInternal::StopWatch _watch;
};

class ObserverI : virtual public Ice::Instrumentation::Observer, public ObserverT<Metrics>
{
};

template<typename T> Updater*
newUpdater(const IceUtil::Handle<T>& updater, void (T::*fn)())
{
    return new UpdaterT<T>(updater.get(), fn);
}

template<typename T> Updater*
newUpdater(const IceInternal::Handle<T>& updater, void (T::*fn)())
{
    return new UpdaterT<T>(updater.get(), fn);
}

template<typename ObserverImplType> 
class ObserverFactoryT
{
public:

    typedef IceUtil::Handle<ObserverImplType> ObserverImplPtrType;
    typedef typename ObserverImplType::Type MetricsType;

    ObserverFactoryT(const MetricsAdminIPtr& metrics, const std::string& name) : _metrics(metrics), _name(name)
    {
        _metrics->registerMap<MetricsType>(name);
    }

    ObserverImplPtrType
    getObserver(const MetricsHelperT<MetricsType>& helper)
    {
        std::vector<MetricsMapI::EntryPtr> metricsObjects = _metrics->getMatching(_name, helper);
        if(metricsObjects.empty())
        {
            return 0;
        }

        ObserverImplPtrType obsv = new ObserverImplType();
        obsv->update(helper, metricsObjects);
        return obsv;
    }

    template<typename ObserverPtrType> ObserverImplPtrType
    getObserver(const MetricsHelperT<MetricsType>& helper, const ObserverPtrType& observer)
    {
        std::vector<MetricsMapI::EntryPtr> metricsObjects = _metrics->getMatching(_name, helper);
        if(metricsObjects.empty())
        {
            return 0;
        }

        ObserverImplPtrType obsv = ObserverImplPtrType::dynamicCast(observer);
        if(!obsv)
        {
            obsv = new ObserverImplType();
        }
        obsv->update(helper, metricsObjects);
        return obsv;
    }

    void 
    registerSubMap(const std::string& subMap, MetricsMap MetricsType::* member)
    {
        _metrics->registerSubMap<MetricsType>(_name, subMap, member);
    }

private:

    const MetricsAdminIPtr _metrics;
    const std::string _name;
};

}

#endif
