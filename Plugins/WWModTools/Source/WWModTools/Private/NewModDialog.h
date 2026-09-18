#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "ModActions.h"

class SNewModDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNewModDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Returns false if cancelled.
	static bool ShowModal(FModInfo& OutInfo);

private:
	FReply OnOKClicked();
	FReply OnCancelClicked();
	bool IsOKEnabled() const;

	TSharedRef<SWidget> MakeField(const TCHAR* Label, const TCHAR* Hint, TSharedPtr<SEditableTextBox>& OutBox);

	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> DisplayNameBox;
	TSharedPtr<SEditableTextBox> DescriptionBox;
	TSharedPtr<SEditableTextBox> VersionBox;
	TSharedPtr<SEditableTextBox> CreatedByBox;
	TWeakPtr<SWindow>            ParentWindow;
	FModInfo                     Result;
	bool                         bConfirmed = false;
};
