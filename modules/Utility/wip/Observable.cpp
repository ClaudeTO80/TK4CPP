#include "Poco/NObserver.h"
#include "Poco/Notification.h"
#include "Poco/NotificationCenter.h"

#include "Poco/BasicEvent.h" 
#include "Poco/Delegate.h" 
#include "Poco/PriorityDelegate.h" 
#include "Poco/PriorityEvent.h" 

#include <map>
#include <chrono>

using Poco::BasicEvent;
using Poco::Delegate;

namespace TK4CPP
{
	namespace Utility
	{
		enum class Priority
		{
			Top,
			VeryHigh,
			High,
			Normal,
			Low,
			VeryLow
		};

		template<typename T>
		class MyEvent : public BasicEvent<T>
		{
		public:

		private:

		};

		class LogEvent : public MyEvent<std::string>
		{
		public:
			void log(const std::string& msgIn) {
				/**
				Get the current time
				*/
				auto now = std::chrono::system_clock::now();
				std::time_t now_c = std::chrono::system_clock::to_time_t(now);
				// Format the time as a string
				char timeStr[100];
				std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));
				// Build the message
				std::string message(std::string(timeStr) + " - " + msgIn);

				this->notify(this, message);
			}
		};
		class MyEventSet
		{
		public:
			void addEvent(int id, const MyEvent<int>& event) {
				m_events[id] = event;
			}
		private:
			std::map<int, MyEvent> m_events;
		};
		class Source
		{
		public:


			void fireEvent(int n) {
				theEvent(this, n);
			}
			// theEvent.notify(this, n); // alternative syntax 
		private:
			MyEvent<int> theEvent;
		};

		class Target
		{
		public: void onEvent(const void* pSender, int& arg) { std::cout << "onEvent: " << arg << std::endl; }
		};
	}
}

using namespace TK4CPP::Utility;

int main(int argc, char** argv)
{
	Source source;
	Target target;
	source.theEvent += Poco::PriorityDelegate(&target, &Target::onEvent);
	source.fireEvent(42);
	source.theEvent -= Poco::delegate(&target, &Target::onEvent);
	return 0;
}
//
//namespace TK4CPP
//{
//	namespace Utility
//	{
//
//		class Notification : public Poco::Notification {
//
//		public:
//			using Ptr = Poco::AutoPtr<Notification>;
//			Notification(const std::string& name) : Poco::Notification(name) {}
//		};
//
//		class NotificationCenter : public Poco::NotificationCenter {
//		};
//
//		class Observable {
//		public:
//			template<typename O, typename N>
//			//void addObserver(Poco::Observer<Observable, Notification> observer) {
//			void addObserver(Poco::Observer<Observable, Notification> observer) {
//				Poco::NObserver<O, N>
//					_center.addObserver(observer);
//			}
//			void removeObserver(Poco::Observer<Observable, Notification> observer) {
//				_center.removeObserver(observer);
//			}
//			void notify(const std::string& message) {
//				_center.postNotification(new Notification(message));
//			}
//
//		private:
//			NotificationCenter _center;
//		};
//
//		class Observer {
//
//		public:
//			void handleNotification(const Poco::AutoPtr<Notification>& pNf) {
//				std::cout << "Received notification: " << pNf->name() << std::endl;
//			}
//
//			void subscribe(Observable& observable) {
//				observable.addObserver(Poco::NObserver<Observer, Notification>(*this, &Observer::handleNotification));
//			}
//		};
//	}
//}