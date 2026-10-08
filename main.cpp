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
    
    Incident(){}
    Incident(int id,int severity,int peopleAffected,int waitingTime,string status,string location,string type){
        this->id=id;
        this->severity=severity;
        this->peopleAffected=peopleAffected;
        this->waitingTime=waitingTime;
        this->status=status;
        this->location=location;
        this->type = type;
    };
    void getdata(){
        cout<<"\nEnter Incident id: ";
        cin>>id;
        cout<<"\nEnter severity: ";
        cin>>severity;
        cout<<"\nEnter People Affected: ";
        cin>>peopleAffected;
        cout<<"\nEnter waiting time: ";
        cin>>waitingTime;
        cout<<"\nEnter status: ";
        cin.ignore();
        cin>>status;
        cout<<"\nEnter location: ";
        cin.ignore();
        cin>>location;
        cout<<"\nEnter Incident type: ";
        cin.ignore();
        cin>>type;
    }
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
    char ch;
    do{
        cout<<"1.Enter Incident\n2.Exit\nEnter Choice: ";
        cin>>ch;
        Incident i;
        i.getdata();
        p.push(i);
        cout<<"\nContinue ? ---Y/N ";
    }while(ch!='n' || ch!='N');

    while(!p.empty()){
        Incident s=p.top();
        p.pop();
        cout << "Incident ID: " << s.id << endl;
        cout << "Priority: " << s.priority << endl;
        cout << "Type: " << s.type << endl;
        cout << "Location: " << s.location << endl;
        cout << "\n";
    }
    return 0;
}