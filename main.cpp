#include<iostream>
#include<string>
#include<queue>
#include<vector>
using namespace std;

class Incident {
    public:
        int id;
        int severity;
        int peopleAffected;
        int priority;
        int waitingTime;
        string status;
        string location;
        string type;
    Incident(int id,int severity,int peopleAffected,int waitingTime,string status,string location,string type){
        this->id=id;
        this->severity=severity;
        this->peopleAffected=peopleAffected;
        this->waitingTime=waitingTime;
        this->status=status;
        this->location=location;  
    };
    void calculatePriority(){
        priority=severity*10 + peopleAffected*2 + waitingTime; 
    }
};
struct compareIncident{
    bool operator()(const Incident& s1,const Incident &s2){
        return s1.priority < s2.priority;
    }
};

int main(){
    priority_queue<Incident, vector<Incident> ,compareIncident> p;
    while(!p.empty()){
        Incident s=p.top();
        p.pop();
    }
    return 0;
}