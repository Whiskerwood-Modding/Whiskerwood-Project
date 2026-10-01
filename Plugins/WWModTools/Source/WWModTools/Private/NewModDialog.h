#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "ModActions.h"

class SNewModDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNewModDialog)
		: _bLockName(false)
	{}
		SLATE_ARGUMENT(FModInfo, InitialInfo)
		// True when the mod folder already exists, so its name cannot be changed here
		SLATE_ARGUMENT(bool, bLockName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Returns false if cancelled
	static bool ShowModal(FModInfo& InOutInfo, const FText& Title, bool bLockName = false);

private:
	FReply OnOKClicked();
	FReply OnCancelClicked();
	bool IsOKEnabled() const;

	TSharedRef<SWidget> MakeField(const FModFieldDef& Def, const FString& InitialValue);

	TSharedPtr<SEditableTextBox> NameBox;

	// One box per editable field, keyed by the field's JSON key
	TMap<FString, TSharedPtr<SEditableTextBox>> FieldBoxes;

	TWeakPtr<SWindow>            ParentWindow;
	FModInfo                     Result;
	bool                         bConfirmed = false;
};
