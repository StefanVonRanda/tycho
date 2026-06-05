package main
import "fmt"
func main(){
  var n,k,total int64 = 2000,2000,0
  for p:=int64(0);p<k;p++{
    fs:=make([]func(int64)int64,0,n)
    for i:=int64(0);i<n;i++{ ii,pp:=i,p; fs=append(fs,func(x int64)int64{return x*(ii+1)+pp}) }
    for _,f:=range fs { total=(total+f(7))&1048575 }
  }
  fmt.Println(total)
}
